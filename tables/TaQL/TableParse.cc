//# Copyright (C) 1994,1995,1997,1998,1999,2000,2001,2003
//# Associated Universities, Inc. Washington DC, USA.
//#
//# This library is free software; you can redistribute it and/or modify it
//# under the terms of the GNU Library General Public License as published by
//# the Free Software Foundation; either version 2 of the License, or (at your
//# option) any later version.
//#
//# This library is distributed in the hope that it will be useful, but WITHOUT
//# ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
//# FITNESS FOR A PARTICULAR PURPOSE.  See the GNU Library General Public
//# License for more details.
//#
//# You should have received a copy of the GNU Library General Public License
//# along with this library; if not, write to the Free Software Foundation,
//# Inc., 675 Massachusetts Ave, Cambridge, MA 02139, USA.
//#
//# Correspondence concerning AIPS++ should be addressed as follows:
//#        Internet email: aips2-request@nrao.edu.
//#        Postal address: AIPS++ Project Office
//#                        National Radio Astronomy Observatory
//#                        520 Edgemont Road
//#                        Charlottesville, VA 22903-2475 USA
//#
//# $Id: TableParse.cc 21399 2013-11-12 07:55:35Z gervandiepen $

#include <casacore/tables/TaQL/TaQLNode.h>
#include <casacore/tables/TaQL/TaQLNodeHandler.h>
#include <casacore/tables/TaQL/TaQLStyle.h>
#include <casacore/tables/TaQL/TableParse.h>
#include <casacore/tables/TaQL/TableGram.h>
#include <casacore/tables/TaQL/TaQLResult.h>
#include <casacore/tables/TaQL/ExprNode.h>
#include <casacore/tables/TaQL/ExprDerNode.h>
#include <casacore/tables/TaQL/ExprDerNodeArray.h>
#include <casacore/tables/TaQL/ExprNodeSet.h>
#include <casacore/tables/TaQL/ExprAggrNode.h>
#include <casacore/tables/TaQL/ExprConeNode.h>
#include <casacore/tables/TaQL/ExprUnitNode.h>
#include <casacore/tables/TaQL/ExprGroupAggrFunc.h>
#include <casacore/tables/TaQL/ExprRange.h>
#include <casacore/tables/TaQL/TableExprIdAggr.h>
#include <casacore/tables/Tables/TableUtil.h>
#include <casacore/tables/Tables/TableColumn.h>
#include <casacore/tables/Tables/ScalarColumn.h>
#include <casacore/tables/Tables/ArrayColumn.h>
#include <casacore/tables/Tables/TableCopy.h>
#include <casacore/tables/Tables/TableUtil.h>
#include <casacore/tables/Tables/TableIter.h>
#include <casacore/tables/Tables/TableRow.h>
#include <casacore/tables/Tables/TableRecord.h>
#include <casacore/tables/Tables/TableDesc.h>
#include <casacore/tables/Tables/ColumnDesc.h>
#include <casacore/tables/Tables/ScaColDesc.h>
#include <casacore/tables/Tables/ArrColDesc.h>
#include <casacore/tables/Tables/SetupNewTab.h>
#include <casacore/tables/DataMan/StandardStMan.h>
#include <casacore/tables/DataMan/DataManInfo.h>
#include <casacore/tables/Tables/TableError.h>
#include <casacore/casa/Arrays/Vector.h>
#include <casacore/casa/Arrays/ArrayMath.h>
#include <casacore/casa/IO/ArrayIO.h>
#include <casacore/casa/Utilities/ValType.h>
#include <casacore/casa/Utilities/Sort.h>
#include <casacore/casa/Utilities/GenSort.h>
#include <casacore/casa/Utilities/LinearSearch.h>
#include <casacore/casa/Utilities/Assert.h>
#include <casacore/casa/IO/AipsIO.h>
#include <casacore/casa/OS/Timer.h>
#include <casacore/casa/ostream.h>
#include <algorithm>


namespace casacore { //# NAMESPACE CASACORE - BEGIN


//# Default constructor.
TableParse::TableParse()
{}

//# Constructor with given table name and possible shorthand.
TableParse::TableParse (const Table& table, Int tabnr, const String& name,
                          const String& shorthand)
  : tabnr_p     (tabnr),
    name_p      (name),
    shorthand_p (shorthand),
    table_p     (table)
{}



TableParseUpdate::TableParseUpdate (const String& columnName,
                                    const String& columnNameMask,
                                    const TableExprNode& node,
                                    Bool checkAggr)
  : columnName_p     (columnName),
    columnNameMask_p (columnNameMask),
    maskFirst_p      (False),
    indexPtr_p       (0),
    node_p           (node)
{
  if (checkAggr) {
    TableParseSelect::checkAggrFuncs (node);
  }
}
TableParseUpdate::TableParseUpdate (const String& columnName,
                                    const String& columnNameMask,
                                    const TableExprNodeSet& indices,
                                    const TableExprNode& node,
                                    const TaQLStyle& style)
  : columnName_p     (columnName),
    columnNameMask_p (columnNameMask),
    maskFirst_p      (False),
    indexPtr_p       (0),
    node_p           (node)
{
  TableParseSelect::checkAggrFuncs (node);
  handleIndices (indices, style);
  if (indexPtr_p == 0) {
    if (! columnNameMask_p.empty()) {
      throw TableInvExpr ("No mask column name can be given if the update "
                          "data column is masked");
    }
    maskFirst_p = True;
  }
}
TableParseUpdate::TableParseUpdate (const String& columnName,
                                    const String& columnNameMask,
                                    const TableExprNodeSet& indices1,
                                    const TableExprNodeSet& indices2,
                                    const TableExprNode& node,
                                    const TaQLStyle& style)
  : columnName_p     (columnName),
    columnNameMask_p (columnNameMask),
    maskFirst_p      (False),
    indexPtr_p       (0),
    node_p           (node)
{
  // The grammar does not allow a column mask name, but you can never tell.
  AlwaysAssert (columnNameMask.empty(), AipsError);
  TableParseSelect::checkAggrFuncs (node);
  handleIndices (indices1, style);
  maskFirst_p = indexPtr_p==0;
  handleIndices (indices2, style);
}
void TableParseUpdate::handleIndices (const TableExprNodeSet& indices,
                                      const TaQLStyle& style)
{
  // Create a mask if a single bool element is given.
  if (indices.isSingle()  &&  indices.size() == 1  &&
      indices.dataType() == TableExprNodeRep::NTBool) {
    if (! mask_p.isNull()) {
      throw TableInvExpr ("A double indexed update array cannot contain "
                          "two masks");
    }
    if (! indices.hasArrays()) {
      throw TableInvExpr ("A mask in an update must be an array");
    }
    mask_p = TableExprNode(indices[0].start());
  } else {
    if (indexPtr_p) {
      throw TableInvExpr ("A double indexed update array cannot contain "
                          "two index ranges");
    }
    indexPtr_p  = new TableExprNodeIndex (indices, style);
    indexNode_p = TableExprNode(indexPtr_p);
  }
}
TableParseUpdate::~TableParseUpdate()
{
  // indexPtr_p does not need to be deleted because it is part of indexNode_p.
}



TableParseSort::TableParseSort()
  : order_p (Sort::Ascending),
    given_p (False)
{}
TableParseSort::TableParseSort (const TableExprNode& node)
  : node_p  (node),
    order_p (Sort::Ascending),
    given_p (False)
{
  checkNode();
}
TableParseSort::TableParseSort (const TableExprNode& node, Sort::Order order)
  : node_p  (node),
    order_p (order),
    given_p (True)
{
  checkNode();
}
TableParseSort::~TableParseSort()
{}
void TableParseSort::checkNode() const
{
  if (! node_p.isScalar()) {
    throw TableInvExpr("ORDERBY column/expression must be a scalar");
  }
  TableParseSelect::checkAggrFuncs (node_p);
}



TableParseSelect::TableParseSelect (CommandType commandType)
  : commandType_p   (commandType),
    tableDesc_p     (new TableDesc()),
    nrSelExprUsed_p (0),
    distinct_p      (False),
    resultType_p    (0),
    resultCreated_p (False),
    endianFormat_p  (Table::AipsrcEndian),
    overwrite_p     (True),
    resultSet_p     (0),
    groupbyRollup_p (False),
    limit_p         (0),
    endrow_p        (0),
    offset_p        (0),
    stride_p        (1),
    insSel_p        (0),
    noDupl_p        (False),
    order_p         (Sort::Ascending)
{}

TableParseSelect::~TableParseSelect()
{
  // Note that insSel_p is simply a pointer to another object,
  // so no delete should be done.
  delete resultSet_p;
}


//# Construct a TableParse object and add it to the container.
Table TableParseSelect::addTable (Int tabnr, const String& name,
                                  const Table& ftab,
                                  const String& shorthand,
                                  Bool addToFromList,
                                  const std::vector<const Table*>& tempTables,
                                  const std::vector<TableParseSelect*>& stack)
{
  Table table = getTable (tabnr, name, ftab, tempTables, stack);
  if (addToFromList) {
    fromTables_p.push_back (TableParse(table, tabnr, name, shorthand));
  } else {
    withTables_p.push_back (TableParse(table, tabnr, name, shorthand));
  }
  return table;
}

// Handle a table name and create a Table object for it as needed.
// This is quite complex, because a table name can be given in many ways:
// 1. an ordinary name such as 'my.tab'
// 2. a wildcarded name (for table concatenation) such as 'my*.tab'. Note that for
//    such a case the alwaysOpen=False, so no Table object is created.
// 3. a table number in the temporary table list such as $1
// 4. a shorthand referring to another table at this or a higher query level
// 5.  :: or . indicating the first available table at this or a higher query level
// 6.  a Table object resulting from a nested query
// 7. a subtable indicated by a keyword such as tabspec::sub or tabspec::sub1::sub2
//    where tabspec can be a table name as in 1, 3, 4 or 5 above.
//    - the subtable can be a table keyword as above, but also a column keyword
//      such as shorthand.column::key. Note that a column can only be given after
//      a shorthand to distinguish it from an ordinary table name.
//      The first part before a . is tried as a shorthand and can be empty indicating
//      the first available table as in 5.
//    - keywords can be nested thus tab::key1.key2.key3
//      It means that sh.col::a1.a2.s1::b1.s2::c1.c2.c3.s4 is a valid specification
//      and indicates subtable s4 in subtable s3 in subtable s1 using nested
//      keywords in column col. But this example is very esoteric.
// In practice column keywords and nested keywords will hardly ever be used,
// so usually something like my.ms::ANTENNA is the only 'complicated' spec used.
Table TableParseSelect::getTable (Int tabnr, const String& name,
                                  const Table& ftab,
                                  const std::vector<const Table*>& tempTables,
                                  const std::vector<TableParseSelect*>& stack,
                                  Bool alwaysOpen)
{
  // A table from a nested query.
  if (! ftab.isNull()) {
    return ftab;
  }
  // Split the name into its subtable parts using :: as separator.
  Table table;
  uInt stSub  = 0;
  uInt stPart = 0;
  Vector<String> subs = stringToVector(name, std::regex("::"));
  // No part, except first one, can be empty (unless :: is given).
  if (name != "::") {
    if (subs.size() == 0  ||
        (subs.size() > 1  &&  anyEQ(subs(Slice(1, subs.size()-1)), String()))) {
      throw TableInvExpr("'"+ name + "' is an invalid table name specification");
    }
  }
  // Split the first subtable name into parts using a dot as separator.
  // The first part can be empty, a shorthand or a temporary table number.
  // An empty part means the first available table.
  stPart = 1;          // indicate first part is handled.
  Vector<String> parts = stringToVector(subs[0], '.');
  if (parts.size() == 0  ||  parts[0].empty()) {
    table = findTable (String(), True, stack);
    if (table.isNull()) {
      throw TableInvExpr(":: or . is invalid in table name " + name +
                         ": no previous table available");
    }
  } else {
    if (tabnr >= 0) {
      // Temporary table number (1-based) given.
      if (tabnr < 1  ||  tabnr > Int(tempTables.size())
          ||  tempTables[tabnr-1] == 0) {
        throw (TableInvExpr ("Invalid temporary table number given in " + name));
      }
      table = *(tempTables[tabnr-1]);
    } else {
      // See if the first part is a shorthand.
      table = findTable (parts[0], True, stack);
      if (table.isNull()) {
        // It was not something like shorthand.column, thus try as a full name.
        // However, do not open if alwaysOpen=False.
        // In that case the table is opened when needed
        // (because the name can contain a wildcard as well).
        if (!alwaysOpen) {
          return table;
        }
        stPart = 0;
        stSub = 1;
        table = Table (subs[0]);
        if (table.isNull()) {
          throw TableInvExpr("Table " + subs[0] + " is unknown");
        }
      }
    }
  }
  // Okay; we have found the first table.
  AlwaysAssert (!table.isNull(), AipsError);
  // Now process all parts in all subtable names, where the first name or
  // first part might need to be skipped because already processed.
  const TableRecord* keywords = &(table.keywordSet());
  for (uInt k=stSub; k<subs.size(); ++k) {
    Vector<String> parts = stringToVector(subs[k], '.');
    for (uInt p=stPart; p<parts.size(); ++p) {
      // See if the first part is a column name. If so, it must be the last part
      // in this name, but not be the last name.
      if (k<subs.size()-1  &&  stPart==parts.size()-1  &&
          table.tableDesc().isColumn(parts[p])) {
        keywords = &(table.tableDesc()[parts[p]].keywordSet());
      } else if (subs[k] != ".") {
        // . indicates first available table.
        // The last keyword must be a Table; the others nested TableRecords.
        Int fieldNr = keywords->fieldNumber(parts[p]);
        if (fieldNr < 0) {
          throw TableInvExpr(parts[p] + " is an unknown keyword/subtable" +
                             (p==1 && k<subs.size()-1 ? " (or column)" : "") +
                             " in " + name);
        } else {
          DataType dtype = keywords->dataType (fieldNr);
          if (p == parts.size()-1) {
            if (dtype != TpTable) {
              throw TableInvExpr(parts[p] + " is no table keyword in " + name);
            }
            table = keywords->asTable (fieldNr);
            keywords = &(table.keywordSet());
          } else {
            if (dtype != TpRecord) {
              throw TableInvExpr(parts[p] + " is no record keyword in " + name);
            }
            keywords = &(keywords->subRecord (fieldNr));
          }
        }
      }
    }
    stPart = 0;
  }
  return table;
}

void TableParseSelect::replaceTable (const Table& table)
{
  AlwaysAssert (!fromTables_p.empty(), AipsError);
  fromTables_p[0].replaceTable (table);
}


// This function can split a name.
// The name can consist of an optional shorthand, a column or keyword name,
// followed by zero or more subfield names (separated by dots).
// In the future it should also be possible to have a subfield name
// followed by a keyword name, etc. to cater for something like:
//   shorthand::key.subtable::key.subsubtable::key.
// If that gets possible, TableGram.ll should also be changed to accept
// such a string in the scanner.
// It is a question whether :: should be part of the scanner or grammar.
// For columns one can go a bit further by accepting something like:
//  col.subtable[select expression resulting in scalar]
// which is something for the far away future.
Bool TableParseSelect::splitName (String& shorthand, String& columnName,
                                  Vector<String>& fieldNames,
                                  const String& name,
                                  Bool checkError,
                                  Bool isKeyword,
                                  Bool allowNoKey)
{
  //# Make a copy, because some String functions are non-const.
  //# Usually the name consists of a columnName only, so use that.
  //# A keyword is given if :: is part of the name or if isKeyword is set.
  shorthand = "";
  columnName = name;
  String restName;
  Bool isKey = isKeyword;
  int j = columnName.index("::");
  Vector<String> fldNam;
  uInt stfld = 0;
  if (j >= 0) {
    // The name contains ::, thus represents a keyword name.
    isKey = True;
  } else if (isKey) {
    // It is a keyword, but no ::.
    j = -2;
  }
  if (isKey) {
    // There should be something after the ::
    // which can be multiple names separated by dots.
    // They represent the keyword name and possible subfields in case
    // the keyword is a record.
    restName = columnName.after(j+1);
    if (!allowNoKey && restName.empty()) {
      if (checkError) {
        throw (TableInvExpr ("No keyword given in name " + name));
      }
      return False;
    }
    fldNam = stringToVector (restName, '.');
    // The part before the :: can be empty, an optional shorthand,
    // and an optional column name (separated by a dot).
    if (j <= 0) {
      columnName = "";
    } else {
      Vector<String> scNames = stringToVector(columnName.before(j), '.');
      switch (scNames.size()) {
      case 2:
        shorthand = scNames(0);
        columnName = scNames(1);
        break;
      case 1:
        columnName = scNames(0);
        break;
      default:
        if (checkError) {
          throw TableInvExpr ("Name " + name + " is invalid: More"
                              " than 2 name parts given before ::");
        }
        return False;
      }
    }
  } else {
    // The name is a column name optionally preceeded by a shorthand
    // and optionally followed by subfields in case the column contains
    // records. The separator is a dot.
    // A name like a.b is in principle ambiguous because:
    // - it can be shorthand.column
    // - it can be column.subfield
    // It is assumed to be a shorthand.
    // Users can use column.subfield by preceeding it with a dot
    // (.a.b always means column.subfield).
    fldNam = stringToVector (columnName, '.');
    if (fldNam.size() == 1) {
      stfld = 0;                      // one part simply means column
    } else if (fldNam(0).empty()) {
      stfld = 1;                      // .column was used
    } else {
      shorthand = fldNam(0);      // a known shorthand is used
      stfld = 1;
    }
    columnName = fldNam(stfld++);
    if (columnName.empty()) {
      if (checkError) {
        throw (TableInvExpr ("No column given in name " + name));
      }
      return False;
    }
  }
  fieldNames.resize (fldNam.size() - stfld);
  for (uInt i=stfld; i<fldNam.size(); i++) {
    if (fldNam(i).empty()) {
      if (checkError) {
        throw (TableInvExpr ("Name " + name +
                             " has empty field names"));
      }
      return False;
    }
    fieldNames(i-stfld) = fldNam(i);
  }
  return isKey;
}

Table TableParseSelect::findTable (const String& shorthand, Bool doWith,
                                   const std::vector<TableParseSelect*>& stack) const
{
  Table table;
  for (Int i=stack.size()-1; i>=0; i--) {
    table = stack[i]->findTable (shorthand, doWith);
    if (! table.isNull()) {
      break;
    }
  }
  return table;
}

Table TableParseSelect::findTable (const String& shorthand, Bool doWith) const
{
  //# If no shorthand given, first table is taken (if there).
  for (uInt i=0; i<fromTables_p.size(); i++) {
    if (fromTables_p[i].test (shorthand)) {
      return fromTables_p[i].table();
    }
  }
  if (doWith) {
    for (uInt i=0; i<withTables_p.size(); i++) {
      if (withTables_p[i].test (shorthand)) {
        return withTables_p[i].table();
      }
    }
  }
  return Table();
}

//# Lookup a field name in the table for which the shorthand is given.
//# If no shorthand is given, use the first table.
//# The shorthand and name are separated by a period.
TableExprNode TableParseSelect::handleKeyCol (const String& name, Bool tryProj)
{
  //# Split the name into optional shorthand, column, and optional keyword.
  String shand, columnName;
  Vector<String> fieldNames;
  Bool hasKey = splitName (shand, columnName, fieldNames, name, True, False,
                           False);
  //# Use first table if there is no shorthand given.
  //# Otherwise find the table at the current level (no WITH tables).
  Table tab = findTable (shand, False);
  if (tab.isNull()) {
    throw (TableInvExpr("Shorthand " + shand + " has not been defined in FROM clause"));
    return 0;
  }
  //# If :: is not given, we have a column or keyword.
  if (!hasKey) {
    if (tryProj && shand.empty() && fieldNames.empty()) {
      // Only the column name is given; so first try if the column is
      // a new name of a projected column. It can also be a column created
      // from the mask of a masked array.
      Bool found;
      Int inx = linearSearchBrackets (found, columnNames_p, columnName,
                                      columnNames_p.size());
      if (!found) {
        inx = linearSearchBrackets (found, columnNameMasks_p, columnName,
                                    columnNameMasks_p.size());
      }
      if (found) {
        // If a table resulting from projection is used, take column from it.
        if (!projectExprTable_p.isNull()  &&
            projectExprTable_p.tableDesc().isColumn (columnName)) {
          uInt nc = projectExprSubset_p.size();
          projectExprSubset_p.resize (nc+1);
          projectExprSubset_p[nc] = inx;
          return projectExprTable_p.col (columnName);
        } else if (!columnOldNames_p.empty()  &&
                   !columnOldNames_p[inx].empty()) {
          // Possibly the column is renamed, so use the old name.
          columnName = columnOldNames_p[inx];
        }
      }
    }
    // If it is a column, check if all tables used have the same size.
    // Note: the projected table (used above) should not be checked.
    if (tab.tableDesc().isColumn (columnName)) {
      if (firstColTable_p.isNull()) {
        firstColTable_p = tab;
        firstColName_p  = name;
      } else {
        if (tab.nrow() != firstColTable_p.nrow()) {
          throw TableInvExpr ("Nr of rows (" + String::toString(tab.nrow()) +
                              ") in table column " + name +
                              " differs from column "+ firstColName_p + " (" +
                              String::toString(firstColTable_p.nrow()) + ')');
        }
      }
    }
    // Create column or keyword node.
    try {
      TableExprNode node(tab.keyCol (columnName, fieldNames));
      addApplySelNode (node);
      return node;
    } catch (const TableError&) {
      throw TableInvExpr(name + " is an unknown column (or keyword) in table "
                         + tab.tableName());
    }
  }
  //# If no column name, we have a table keyword.
  if (columnName.empty()) {
    return tab.key (fieldNames);
  }
  //# Otherwise we have a column keyword.
  TableColumn col (tab, columnName);
  return TableExprNode::newKeyConst (col.keywordSet(), fieldNames);
}

TableExprNode TableParseSelect::handleSlice (const TableExprNode& array,
                                             const TableExprNodeSet& indices,
                                             const TaQLStyle& style)
{
  // Create a masked array if a single bool element is given.
  if (indices.dataType() == TableExprNodeRep::NTBool) {
    if (! (indices.isSingle()  &&  indices.size() == 1  &&
           indices.hasArrays())) {
      throw TableInvExpr ("Second argument of a masked array must be an array; maybe extra brackets are needed like [1,2][[T,F]]");
    }
    return marray (array, TableExprNode(indices[0].start()));
  }
  return TableExprNode::newArrayPartNode (array, indices, style);
}
 
//# Parse the name of a function.
TableExprFuncNode::FunctionType TableParseSelect::findFunc
                               (const String& name,
                                uInt narguments,
                                const Vector<Int>& ignoreFuncs)
{
  //# Determine the function type.
  //# Use the function name in lower case.
  //# Error if functype in ignoreFuncs or if ignoreFuncs is not empty and
  //# the function is an aggregate one.
  TableExprFuncNode::FunctionType ftype = TableExprFuncNode::piFUNC;
  String funcName (name);
  funcName.downcase();
  if (funcName == "pi") {
    ftype = TableExprFuncNode::piFUNC;
  } else if (funcName == "e") {
    ftype = TableExprFuncNode::eFUNC;
  } else if (funcName == "c") {
    ftype = TableExprFuncNode::cFUNC;
  } else if (funcName == "near") {
    ftype = TableExprFuncNode::near2FUNC;
    if (narguments == 3) {
      ftype = TableExprFuncNode::near3FUNC;
    }
  } else if (funcName == "nearabs") {
    ftype = TableExprFuncNode::nearabs2FUNC;
    if (narguments == 3) {
      ftype = TableExprFuncNode::nearabs3FUNC;
    }
  } else if (funcName == "sin") {
    ftype = TableExprFuncNode::sinFUNC;
  } else if (funcName == "sinh") {
    ftype = TableExprFuncNode::sinhFUNC;
  } else if (funcName == "cos") {
    ftype = TableExprFuncNode::cosFUNC;
  } else if (funcName == "cosh") {
    ftype = TableExprFuncNode::coshFUNC;
  } else if (funcName == "exp") {
    ftype = TableExprFuncNode::expFUNC;
  } else if (funcName == "log"  ||  funcName == "ln") {
    ftype = TableExprFuncNode::logFUNC;
  } else if (funcName == "log10") {
    ftype = TableExprFuncNode::log10FUNC;
  } else if (funcName == "sqrt") {
    ftype = TableExprFuncNode::sqrtFUNC;
  } else if (funcName == "pow") {
    ftype = TableExprFuncNode::powFUNC;
  } else if (funcName == "conj") {
    ftype = TableExprFuncNode::conjFUNC;
  } else if (funcName == "square"  ||  funcName == "sqr") {
    ftype = TableExprFuncNode::squareFUNC;
  } else if (funcName == "cube") {
    ftype = TableExprFuncNode::cubeFUNC;
  } else if (funcName == "min") {
    ftype = TableExprFuncNode::minFUNC;
    if (narguments == 1) {
      ftype = TableExprFuncNode::arrminFUNC;
    }
  } else if (funcName == "max") {
    ftype = TableExprFuncNode::maxFUNC;
    if (narguments == 1) {
      ftype = TableExprFuncNode::arrmaxFUNC;
    }
  } else if (funcName == "norm") {
    ftype = TableExprFuncNode::normFUNC;
  } else if (funcName == "abs"  ||  funcName == "amplitude"  ||
             funcName == "ampl") {
    ftype = TableExprFuncNode::absFUNC;
  } else if (funcName == "arg"  ||  funcName == "phase") {
    ftype = TableExprFuncNode::argFUNC;
  } else if (funcName == "real") {
    ftype = TableExprFuncNode::realFUNC;
  } else if (funcName == "imag") {
    ftype = TableExprFuncNode::imagFUNC;
  } else if (funcName == "int"  ||  funcName == "integer") {
    ftype = TableExprFuncNode::intFUNC;
  } else if (funcName == "asin") {
    ftype = TableExprFuncNode::asinFUNC;
  } else if (funcName == "acos") {
    ftype = TableExprFuncNode::acosFUNC;
  } else if (funcName == "atan") {
    ftype = TableExprFuncNode::atanFUNC;
  } else if (funcName == "atan2") {
    ftype = TableExprFuncNode::atan2FUNC;
  } else if (funcName == "tan") {
    ftype = TableExprFuncNode::tanFUNC;
  } else if (funcName == "tanh") {
    ftype = TableExprFuncNode::tanhFUNC;
  } else if (funcName == "sign") {
    ftype = TableExprFuncNode::signFUNC;
  } else if (funcName == "round") {
    ftype = TableExprFuncNode::roundFUNC;
  } else if (funcName == "floor") {
    ftype = TableExprFuncNode::floorFUNC;
  } else if (funcName == "ceil") {
    ftype = TableExprFuncNode::ceilFUNC;
  } else if (funcName == "fmod") {
    ftype = TableExprFuncNode::fmodFUNC;
  } else if (funcName == "complex"  ||  funcName == "formcomplex") {
    ftype = TableExprFuncNode::complexFUNC;
  } else if (funcName == "sum") {
    ftype = TableExprFuncNode::arrsumFUNC;
  } else if (funcName == "sums") {
    ftype = TableExprFuncNode::arrsumsFUNC;
  } else if (funcName == "runningsum") {
    ftype = TableExprFuncNode::runsumFUNC;
  } else if (funcName == "boxedsum") {
    ftype = TableExprFuncNode::boxsumFUNC;
  } else if (funcName == "product") {
    ftype = TableExprFuncNode::arrproductFUNC;
  } else if (funcName == "products") {
    ftype = TableExprFuncNode::arrproductsFUNC;
  } else if (funcName == "runningproduct") {
    ftype = TableExprFuncNode::runproductFUNC;
  } else if (funcName == "boxedproduct") {
    ftype = TableExprFuncNode::boxproductFUNC;
  } else if (funcName == "sumsqr"  ||  funcName == "sumsquare") {
    ftype = TableExprFuncNode::arrsumsqrFUNC;
  } else if (funcName == "sumsqrs"  ||  funcName == "sumsquares") {
    ftype = TableExprFuncNode::arrsumsqrsFUNC;
  } else if (funcName == "runningsumsqr"  ||  funcName == "runningsumsquare") {
    ftype = TableExprFuncNode::runsumsqrFUNC;
  } else if (funcName == "boxedsumsqr"  ||  funcName == "boxedsumsquare") {
    ftype = TableExprFuncNode::boxsumsqrFUNC;
  } else if (funcName == "mins") {
    ftype = TableExprFuncNode::arrminsFUNC;
  } else if (funcName == "runningmin") {
    ftype = TableExprFuncNode::runminFUNC;
  } else if (funcName == "boxedmin") {
    ftype = TableExprFuncNode::boxminFUNC;
  } else if (funcName == "maxs") {
    ftype = TableExprFuncNode::arrmaxsFUNC;
  } else if (funcName == "runningmax") {
    ftype = TableExprFuncNode::runmaxFUNC;
  } else if (funcName == "boxedmax") {
    ftype = TableExprFuncNode::boxmaxFUNC;
  } else if (funcName == "mean"  ||  funcName == "avg") {
    ftype = TableExprFuncNode::arrmeanFUNC;
  } else if (funcName == "means"  ||  funcName == "avgs") {
    ftype = TableExprFuncNode::arrmeansFUNC;
  } else if (funcName == "runningmean"  ||  funcName == "runningavg") {
    ftype = TableExprFuncNode::runmeanFUNC;
  } else if (funcName == "boxedmean"  ||  funcName == "boxedavg") {
    ftype = TableExprFuncNode::boxmeanFUNC;
  } else if (funcName == "variance") {
    ftype = TableExprFuncNode::arrvariance0FUNC;
  } else if (funcName == "variances") {
    ftype = TableExprFuncNode::arrvariances0FUNC;
  } else if (funcName == "runningvariance") {
    ftype = TableExprFuncNode::runvariance0FUNC;
  } else if (funcName == "boxedvariance") {
    ftype = TableExprFuncNode::boxvariance0FUNC;
  } else if (funcName == "samplevariance") {
    ftype = TableExprFuncNode::arrvariance1FUNC;
  } else if (funcName == "samplevariances") {
    ftype = TableExprFuncNode::arrvariances1FUNC;
  } else if (funcName == "runningsamplevariance") {
    ftype = TableExprFuncNode::runvariance1FUNC;
  } else if (funcName == "boxedsamplevariance") {
    ftype = TableExprFuncNode::boxvariance1FUNC;
  } else if (funcName == "stddev") {
    ftype = TableExprFuncNode::arrstddev0FUNC;
  } else if (funcName == "stddevs") {
    ftype = TableExprFuncNode::arrstddevs0FUNC;
  } else if (funcName == "runningstddev") {
    ftype = TableExprFuncNode::runstddev0FUNC;
  } else if (funcName == "boxedstddev") {
    ftype = TableExprFuncNode::boxstddev0FUNC;
  } else if (funcName == "samplestddev") {
    ftype = TableExprFuncNode::arrstddev1FUNC;
  } else if (funcName == "samplestddevs") {
    ftype = TableExprFuncNode::arrstddevs1FUNC;
  } else if (funcName == "runningsamplestddev") {
    ftype = TableExprFuncNode::runstddev1FUNC;
  } else if (funcName == "boxedsamplestddev") {
    ftype = TableExprFuncNode::boxstddev1FUNC;
  } else if (funcName == "avdev") {
    ftype = TableExprFuncNode::arravdevFUNC;
  } else if (funcName == "avdevs") {
    ftype = TableExprFuncNode::arravdevsFUNC;
  } else if (funcName == "runningavdev") {
    ftype = TableExprFuncNode::runavdevFUNC;
  } else if (funcName == "boxedavdev") {
    ftype = TableExprFuncNode::boxavdevFUNC;
  } else if (funcName == "rms") {
    ftype = TableExprFuncNode::arrrmsFUNC;
  } else if (funcName == "rmss") {
    ftype = TableExprFuncNode::arrrmssFUNC;
  } else if (funcName == "runningrms") {
    ftype = TableExprFuncNode::runrmsFUNC;
  } else if (funcName == "boxedrms") {
    ftype = TableExprFuncNode::boxrmsFUNC;
  } else if (funcName == "median") {
    ftype = TableExprFuncNode::arrmedianFUNC;
  } else if (funcName == "medians") {
    ftype = TableExprFuncNode::arrmediansFUNC;
  } else if (funcName == "runningmedian") {
    ftype = TableExprFuncNode::runmedianFUNC;
  } else if (funcName == "boxedmedian") {
    ftype = TableExprFuncNode::boxmedianFUNC;
  } else if (funcName == "fractile") {
    ftype = TableExprFuncNode::arrfractileFUNC;
  } else if (funcName == "fractiles") {
    ftype = TableExprFuncNode::arrfractilesFUNC;
  } else if (funcName == "runningfractile") {
    ftype = TableExprFuncNode::runfractileFUNC;
  } else if (funcName == "boxedfractile") {
    ftype = TableExprFuncNode::boxfractileFUNC;
  } else if (funcName == "any") {
    ftype = TableExprFuncNode::arranyFUNC;
  } else if (funcName == "anys") {
    ftype = TableExprFuncNode::arranysFUNC;
  } else if (funcName == "runningany") {
    ftype = TableExprFuncNode::runanyFUNC;
  } else if (funcName == "boxedany") {
    ftype = TableExprFuncNode::boxanyFUNC;
  } else if (funcName == "all") {
    ftype = TableExprFuncNode::arrallFUNC;
  } else if (funcName == "alls") {
    ftype = TableExprFuncNode::arrallsFUNC;
  } else if (funcName == "runningall") {
    ftype = TableExprFuncNode::runallFUNC;
  } else if (funcName == "boxedall") {
    ftype = TableExprFuncNode::boxallFUNC;
  } else if (funcName == "ntrue") {
    ftype = TableExprFuncNode::arrntrueFUNC;
  } else if (funcName == "ntrues") {
    ftype = TableExprFuncNode::arrntruesFUNC;
  } else if (funcName == "runningntrue") {
    ftype = TableExprFuncNode::runntrueFUNC;
  } else if (funcName == "boxedntrue") {
    ftype = TableExprFuncNode::boxntrueFUNC;
  } else if (funcName == "nfalse") {
    ftype = TableExprFuncNode::arrnfalseFUNC;
  } else if (funcName == "nfalses") {
    ftype = TableExprFuncNode::arrnfalsesFUNC;
  } else if (funcName == "runningnfalse") {
    ftype = TableExprFuncNode::runnfalseFUNC;
  } else if (funcName == "boxednfalse") {
    ftype = TableExprFuncNode::boxnfalseFUNC;
  } else if (funcName == "array") {
    ftype = TableExprFuncNode::arrayFUNC;
  } else if (funcName == "transpose") {
    ftype = TableExprFuncNode::transposeFUNC;
  } else if (funcName == "reversearray"  ||  funcName == "areverse") {
    ftype = TableExprFuncNode::areverseFUNC;
  } else if (funcName == "diagonal"  ||  funcName == "diagonals") {
    ftype = TableExprFuncNode::diagonalFUNC;
  } else if (funcName == "resize") {
    ftype = TableExprFuncNode::resizeFUNC;
  } else if (funcName == "isnan") {
    ftype = TableExprFuncNode::isnanFUNC;
  } else if (funcName == "isinf") {
    ftype = TableExprFuncNode::isinfFUNC;
  } else if (funcName == "isfinite") {
    ftype = TableExprFuncNode::isfiniteFUNC;
  } else if (funcName == "isdefined") {
    ftype = TableExprFuncNode::isdefFUNC;
  } else if (funcName == "isnull") {
    ftype = TableExprFuncNode::isnullFUNC;
  } else if (funcName == "iscolumn") {
    ftype = TableExprFuncNode::iscolFUNC;
  } else if (funcName == "iskeyword") {
    ftype = TableExprFuncNode::iskeyFUNC;
  } else if (funcName == "ndim") {
    ftype = TableExprFuncNode::ndimFUNC;
  } else if (funcName == "nelements"  ||  funcName == "count") {
    ftype = TableExprFuncNode::nelemFUNC;
  } else if (funcName == "shape") {
    ftype = TableExprFuncNode::shapeFUNC;
  } else if (funcName == "strlength" ||  funcName == "len") {
    ftype = TableExprFuncNode::strlengthFUNC;
  } else if (funcName == "upcase"    ||  funcName == "upper"  ||
             funcName == "toupper"   ||  funcName == "to_upper") {
    ftype = TableExprFuncNode::upcaseFUNC;
  } else if (funcName == "downcase"  ||  funcName == "lower"  ||
             funcName == "tolower"   ||  funcName == "to_lower") {
    ftype = TableExprFuncNode::downcaseFUNC;
  } else if (funcName == "capitalize") {
    ftype = TableExprFuncNode::capitalizeFUNC;
  } else if (funcName == "reversestring"  ||  funcName == "sreverse") {
    ftype = TableExprFuncNode::sreverseFUNC;
  } else if (funcName == "trim") {
    ftype = TableExprFuncNode::trimFUNC;
  } else if (funcName == "ltrim") {
    ftype = TableExprFuncNode::ltrimFUNC;
  } else if (funcName == "rtrim") {
    ftype = TableExprFuncNode::rtrimFUNC;
  } else if (funcName == "substr"  ||  funcName == "substring") {
    ftype = TableExprFuncNode::substrFUNC;
  } else if (funcName == "replace") {
    ftype = TableExprFuncNode::replaceFUNC;
  } else if (funcName == "regex") {
    ftype = TableExprFuncNode::regexFUNC;
  } else if (funcName == "pattern") {
    ftype = TableExprFuncNode::patternFUNC;
  } else if (funcName == "sqlpattern") {
    ftype = TableExprFuncNode::sqlpatternFUNC;
  } else if (funcName == "datetime") {
    ftype = TableExprFuncNode::datetimeFUNC;
  } else if (funcName == "mjdtodate") {
    ftype = TableExprFuncNode::mjdtodateFUNC;
  } else if (funcName == "mjd") {
    ftype = TableExprFuncNode::mjdFUNC;
  } else if (funcName == "date") {
    ftype = TableExprFuncNode::dateFUNC;
  } else if (funcName == "time") {
    ftype = TableExprFuncNode::timeFUNC;
  } else if (funcName == "year") {
    ftype = TableExprFuncNode::yearFUNC;
  } else if (funcName == "month") {
    ftype = TableExprFuncNode::monthFUNC;
  } else if (funcName == "day") {
    ftype = TableExprFuncNode::dayFUNC;
  } else if (funcName == "cmonth") {
    ftype = TableExprFuncNode::cmonthFUNC;
  } else if (funcName == "weekday"   ||  funcName == "dow") {
    ftype = TableExprFuncNode::weekdayFUNC;
  } else if (funcName == "cweekday"   ||  funcName == "cdow") {
    ftype = TableExprFuncNode::cdowFUNC;
  } else if (funcName == "week") {
    ftype = TableExprFuncNode::weekFUNC;
  } else if (funcName == "cdatetime"  ||  funcName == "ctod") {
    ftype = TableExprFuncNode::ctodFUNC;
  } else if (funcName == "cdate") {
    ftype = TableExprFuncNode::cdateFUNC;
  } else if (funcName == "ctime") {
    ftype = TableExprFuncNode::ctimeFUNC;
  } else if (funcName == "string"  ||  funcName == "str") {
    ftype = TableExprFuncNode::stringFUNC;
  } else if (funcName == "hms") {
    ftype = TableExprFuncNode::hmsFUNC;
  } else if (funcName == "dms") {
    ftype = TableExprFuncNode::dmsFUNC;
  } else if (funcName == "hdms") {
    ftype = TableExprFuncNode::hdmsFUNC;
  } else if (funcName == "rand") {
    ftype = TableExprFuncNode::randFUNC;
  } else if (funcName == "rownumber"  ||  funcName == "rownr") {
    ftype = TableExprFuncNode::rownrFUNC;
  } else if (funcName == "rowid") {
    ftype = TableExprFuncNode::rowidFUNC;
  } else if (funcName == "iif") {
    ftype = TableExprFuncNode::iifFUNC;
  } else if (funcName == "angdist"  ||  funcName == "angulardistance") {
    ftype = TableExprFuncNode::angdistFUNC;
  } else if (funcName == "angdistx"  ||  funcName == "angulardistancex") {
    ftype = TableExprFuncNode::angdistxFUNC;
  } else if (funcName == "normangle") {
    ftype = TableExprFuncNode::normangleFUNC;
  } else if (funcName == "cones") {
    ftype = TableExprConeNode::conesFUNC;
    if (narguments == 3) {
      ftype = TableExprConeNode::cones3FUNC;
    }
  } else if (funcName == "anycone") {
    ftype = TableExprConeNode::anyconeFUNC;
    if (narguments == 3) {
      ftype = TableExprConeNode::anycone3FUNC;
    }
  } else if (funcName == "findcone") {
    ftype = TableExprConeNode::findconeFUNC;
    if (narguments == 3) {
      ftype = TableExprConeNode::findcone3FUNC;
    }
  } else if (funcName == "bool"  ||  funcName == "boolean") {
    ftype = TableExprFuncNode::boolFUNC;
  } else if (funcName == "nullarray") {
    ftype = TableExprFuncNode::nullarrayFUNC;
  } else if (funcName == "marray") {
    ftype = TableExprFuncNode::marrayFUNC;
  } else if (funcName == "arraydata") {
    ftype = TableExprFuncNode::arrdataFUNC;
  } else if (funcName == "mask"  ||  funcName == "arraymask") {
    ftype = TableExprFuncNode::arrmaskFUNC;
  } else if (funcName == "negatemask") {
    ftype = TableExprFuncNode::negatemaskFUNC;
  } else if (funcName == "replacemasked") {
    ftype = TableExprFuncNode::replmaskedFUNC;
  } else if (funcName == "replaceunmasked") {
    ftype = TableExprFuncNode::replunmaskedFUNC;
  } else if (funcName == "flatten"  ||  funcName == "arrayflatten") {
    ftype = TableExprFuncNode::arrflatFUNC;
  } else if (funcName == "countall") {
    ftype = TableExprFuncNode::countallFUNC;
  } else if (funcName == "gcount") {
    ftype = TableExprFuncNode::gcountFUNC;
  } else if (funcName == "gfirst") {
    ftype = TableExprFuncNode::gfirstFUNC;
  } else if (funcName == "glast") {
    ftype = TableExprFuncNode::glastFUNC;
  } else if (funcName == "gmin") {
    ftype = TableExprFuncNode::gminFUNC;
  } else if (funcName == "gmins") {
    ftype = TableExprFuncNode::gminsFUNC;
  } else if (funcName == "gmax") {
    ftype = TableExprFuncNode::gmaxFUNC;
  } else if (funcName == "gmaxs") {
    ftype = TableExprFuncNode::gmaxsFUNC;
  } else if (funcName == "gsum") {
    ftype = TableExprFuncNode::gsumFUNC;
  } else if (funcName == "gsums") {
    ftype = TableExprFuncNode::gsumsFUNC;
  } else if (funcName == "gproduct") {
    ftype = TableExprFuncNode::gproductFUNC;
  } else if (funcName == "gproducts") {
    ftype = TableExprFuncNode::gproductsFUNC;
  } else if (funcName == "gsumsqr"  ||  funcName == "gsumsquare") {
    ftype = TableExprFuncNode::gsumsqrFUNC;
  } else if (funcName == "gsumsqrs"  ||  funcName == "gsumsquares") {
    ftype = TableExprFuncNode::gsumsqrsFUNC;
  } else if (funcName == "gmean"  ||  funcName == "gavg") {
    ftype = TableExprFuncNode::gmeanFUNC;
  } else if (funcName == "gmeans"  ||  funcName == "gavgs") {
    ftype = TableExprFuncNode::gmeansFUNC;
  } else if (funcName == "gvariance") {
    ftype = TableExprFuncNode::gvariance0FUNC;
  } else if (funcName == "gvariances") {
    ftype = TableExprFuncNode::gvariances0FUNC;
  } else if (funcName == "gsamplevariance") {
    ftype = TableExprFuncNode::gvariance1FUNC;
  } else if (funcName == "gsamplevariances") {
    ftype = TableExprFuncNode::gvariances1FUNC;
  } else if (funcName == "gstddev") {
    ftype = TableExprFuncNode::gstddev0FUNC;
  } else if (funcName == "gstddevs") {
    ftype = TableExprFuncNode::gstddevs0FUNC;
  } else if (funcName == "gsamplestddev") {
    ftype = TableExprFuncNode::gstddev1FUNC;
  } else if (funcName == "gsamplestddevs") {
    ftype = TableExprFuncNode::gstddevs1FUNC;
  } else if (funcName == "grms") {
    ftype = TableExprFuncNode::grmsFUNC;
  } else if (funcName == "grmss") {
    ftype = TableExprFuncNode::grmssFUNC;
  } else if (funcName == "gany") {
    ftype = TableExprFuncNode::ganyFUNC;
  } else if (funcName == "ganys") {
    ftype = TableExprFuncNode::ganysFUNC;
  } else if (funcName == "gall") {
    ftype = TableExprFuncNode::gallFUNC;
  } else if (funcName == "galls") {
    ftype = TableExprFuncNode::gallsFUNC;
  } else if (funcName == "gntrue") {
    ftype = TableExprFuncNode::gntrueFUNC;
  } else if (funcName == "gntrues") {
    ftype = TableExprFuncNode::gntruesFUNC;
  } else if (funcName == "gnfalse") {
    ftype = TableExprFuncNode::gnfalseFUNC;
  } else if (funcName == "gnfalses") {
    ftype = TableExprFuncNode::gnfalsesFUNC;
  } else if (funcName == "ghist"  ||  funcName == "ghistogram") {
    ftype = TableExprFuncNode::ghistFUNC;
  } else if (funcName == "gaggr"  ||  funcName == "gstack") {
    ftype = TableExprFuncNode::gaggrFUNC;
  } else if (funcName == "growid") {
    ftype = TableExprFuncNode::growidFUNC;
  } else if (funcName == "gmedian") {
    ftype = TableExprFuncNode::gmedianFUNC;
  } else if (funcName == "gfractile") {
    ftype = TableExprFuncNode::gfractileFUNC;
  } else {
    // unknown name can be a user-defined function.
    ftype = TableExprFuncNode::NRFUNC;
  }
  // Functions to be ignored are incorrect.
  Bool found;
  linearSearch (found, ignoreFuncs, Int(ftype), ignoreFuncs.size());
  if (found  ||  (!ignoreFuncs.empty()  &&
                  ftype >= TableExprFuncNode::FirstAggrFunc)) {
    throw (TableInvExpr ("Function '" + funcName +
                         "' can only be used in TaQL"));
  }
  return ftype;
}

//# Parse the name of a function.
TableExprNode TableParseSelect::handleFunc (const String& name,
                                            const TableExprNodeSet& arguments,
                                            const TaQLStyle& style)
{
  //# No functions have to be ignored.
  Vector<Int> ignoreFuncs;
  // Use a default table if no one available.
  if (fromTables_p.size() == 0) {
    return makeFuncNode (this, name, arguments, ignoreFuncs, Table(), style);
  }
  TableExprNode node = makeFuncNode (this, name, arguments, ignoreFuncs,
                                     fromTables_p[0].table(), style);
  // A rowid function node needs to be added to applySelNodes_p.
  const TENShPtr& rep = node.getRep();
  if (dynamic_cast<const TableExprNodeRowid*>(rep.get())) {
    addApplySelNode (node);
  }
  return node;
}

TableExprNode TableParseSelect::makeUDFNode (TableParseSelect* sel,
                                             const String& name,
                                             const TableExprNodeSet& arguments,
                                             const Table& table,
                                             const TaQLStyle& style)
{
  Vector<String> parts = stringToVector (name, '.');
  if (parts.size() == 1) {
    // No ., thus no UDF but a builtin function.
    throw TableInvExpr ("TaQL function " + name + " is unknown; "
                        "use 'show func' to see all functions");
  }
  TableExprNode udf;
  if (sel) {
    if (parts.size() > 2) {
      // At least 3 parts; see if the first part is a table shorthand.
      Table tab = sel->findTable (parts[0], False);
      if (! tab.isNull()) {
        udf = TableExprNode::newUDFNode (name.substr(parts[0].size() + 1),
                                         arguments, tab, style);
      }
    }
  }
  // If not created, use the full name and given (i.e. first) table.
  if (udf.isNull()) {
    udf = TableExprNode::newUDFNode (name, arguments, table, style);
  }
  // A UDF might create table column nodes, so add it to applySelNodes_p.
  if (sel) {
    sel->addApplySelNode (udf);
  }
  return udf;
}

//# Parse the name of a function.
TableExprNode TableParseSelect::makeFuncNode
                                         (TableParseSelect* sel,
                                          const String& fname,
                                          const TableExprNodeSet& arguments,
                                          const Vector<int>& ignoreFuncs,
                                          const Table& tabin,
                                          const TaQLStyle& style)
{
  Table table(tabin);
  String name = fname;
  // See if something like xx.func is given.
  // xx can be a shorthand or a user defined function library.
  Vector<String> parts = stringToVector (name, '.');
  if (sel  &&  parts.size() == 2) {
    // See if xx is a shorthand. If so, use that table.
    Table tab = sel->findTable (parts[0], False);
    if (! tab.isNull()) {
      table = tab;
      name = parts[1];
    }
  }
  //# Determine the function type.
  TableExprFuncNode::FunctionType ftype = findFunc (name,
                                                    arguments.size(),
                                                    ignoreFuncs);
  if (ftype == TableExprFuncNode::NRFUNC) {
    // The function can be a user defined one (or unknown).
    return makeUDFNode (sel, name, arguments, table, style);
  }
  try {
    // The axes of reduction functions like SUMS can be given as a set or as
    // individual values. Turn it into an Array object.
    uInt axarg = 1;
    switch (ftype) {
    case TableExprFuncNode::arrfractilesFUNC:
    case TableExprFuncNode::runfractileFUNC:
    case TableExprFuncNode::boxfractileFUNC:
      axarg = 2;    // fall through!!
    case TableExprFuncNode::arrsumsFUNC:
    case TableExprFuncNode::arrproductsFUNC:
    case TableExprFuncNode::arrsumsqrsFUNC:
    case TableExprFuncNode::arrminsFUNC:
    case TableExprFuncNode::arrmaxsFUNC:
    case TableExprFuncNode::arrmeansFUNC:
    case TableExprFuncNode::arrvariances0FUNC:
    case TableExprFuncNode::arrvariances1FUNC:
    case TableExprFuncNode::arrstddevs0FUNC:
    case TableExprFuncNode::arrstddevs1FUNC:
    case TableExprFuncNode::arravdevsFUNC:
    case TableExprFuncNode::arrrmssFUNC:
    case TableExprFuncNode::arrmediansFUNC:
    case TableExprFuncNode::arranysFUNC:
    case TableExprFuncNode::arrallsFUNC:
    case TableExprFuncNode::arrntruesFUNC:
    case TableExprFuncNode::arrnfalsesFUNC:
    case TableExprFuncNode::runsumFUNC:
    case TableExprFuncNode::runproductFUNC:
    case TableExprFuncNode::runsumsqrFUNC:
    case TableExprFuncNode::runminFUNC:
    case TableExprFuncNode::runmaxFUNC:
    case TableExprFuncNode::runmeanFUNC:
    case TableExprFuncNode::runvariance0FUNC:
    case TableExprFuncNode::runvariance1FUNC:
    case TableExprFuncNode::runstddev0FUNC:
    case TableExprFuncNode::runstddev1FUNC:
    case TableExprFuncNode::runavdevFUNC:
    case TableExprFuncNode::runrmsFUNC:
    case TableExprFuncNode::runmedianFUNC:
    case TableExprFuncNode::runanyFUNC:
    case TableExprFuncNode::runallFUNC:
    case TableExprFuncNode::runntrueFUNC:
    case TableExprFuncNode::runnfalseFUNC:
    case TableExprFuncNode::boxsumFUNC:
    case TableExprFuncNode::boxproductFUNC:
    case TableExprFuncNode::boxsumsqrFUNC:
    case TableExprFuncNode::boxminFUNC:
    case TableExprFuncNode::boxmaxFUNC:
    case TableExprFuncNode::boxmeanFUNC:
    case TableExprFuncNode::boxvariance0FUNC:
    case TableExprFuncNode::boxvariance1FUNC:
    case TableExprFuncNode::boxstddev0FUNC:
    case TableExprFuncNode::boxstddev1FUNC:
    case TableExprFuncNode::boxavdevFUNC:
    case TableExprFuncNode::boxrmsFUNC:
    case TableExprFuncNode::boxmedianFUNC:
    case TableExprFuncNode::boxanyFUNC:
    case TableExprFuncNode::boxallFUNC:
    case TableExprFuncNode::boxntrueFUNC:
    case TableExprFuncNode::boxnfalseFUNC:
    case TableExprFuncNode::arrayFUNC:
    case TableExprFuncNode::transposeFUNC:
    case TableExprFuncNode::areverseFUNC:
    case TableExprFuncNode::diagonalFUNC:
      if (arguments.size() >= axarg) {
        TableExprNodeSet parms;
        // Add first argument(s) to the parms.
        for (uInt i=0; i<axarg; i++) {
          parms.add (arguments[i]);
        }
        // Now handle the axes arguments.
        // They can be given as a set or as individual scalar values.
        Bool axesIsArray = False;
        if (arguments.size() == axarg) {
          // No axes given. Add default one for transpose, etc..
          axesIsArray = True;
          if (ftype == TableExprFuncNode::transposeFUNC  ||
              ftype == TableExprFuncNode::areverseFUNC   ||
              ftype == TableExprFuncNode::diagonalFUNC) {
            // Add an empty vector if no arguments given.
            TableExprNodeSetElem arg((TableExprNode(Vector<Int>())));
            parms.add (arg);
          }
        } else if (arguments.size() == axarg+1
                   &&  arguments[axarg].isSingle()) {
          // A single set given; see if it is an array.
          const TableExprNodeSetElem& arg = arguments[axarg];
          if (arg.start()->valueType() == TableExprNodeRep::VTArray) {
            parms.add (arg);
            axesIsArray = True;
          }
        }
        if (!axesIsArray) {
          // Combine all axes in a single set and add to parms.
          TableExprNodeSet axes;
          for (uInt i=axarg; i<arguments.size(); i++) {
            const TableExprNodeSetElem& arg = arguments[i];
            const TENShPtr& rep = arg.start();
            if (rep == 0  ||  !arg.isSingle()
                ||  rep->valueType() != TableExprNodeRep::VTScalar
                ||  (rep->dataType() != TableExprNodeRep::NTInt
                     &&  rep->dataType() != TableExprNodeRep::NTDouble)) {
              throw TableInvExpr ("Axes/shape arguments " +
                                  String::toString(i+1) +
                                  " are not one or more scalars"
                                  " or a single bounded range");
            }
            axes.add (arg);
          }
          parms.add (TableExprNodeSetElem(axes.setOrArray()));
        }
        return TableExprNode::newFunctionNode (ftype, parms, table, style);
      }
      break;
    case TableExprFuncNode::conesFUNC:
    case TableExprFuncNode::anyconeFUNC:
    case TableExprFuncNode::findconeFUNC:
    case TableExprFuncNode::cones3FUNC:
    case TableExprFuncNode::anycone3FUNC:
    case TableExprFuncNode::findcone3FUNC:
      return TableExprNode::newConeNode (ftype, arguments, style.origin());
    default:
      break;
    }
    return TableExprNode::newFunctionNode (ftype, arguments, table, style);
  } catch (const std::exception& x) {
    String err (x.what());
    if (err.size() > 28  &&  err.before(28) == "Error in select expression: ") {
      err = err.from(28);
    }
    throw TableInvExpr ("Erroneous use of function " + name + " - " + err);
  }
}


//# Add a column name to the block of column names.
//# Only take the part beyond the period.
//# Extend the block each time. Since there are only a few column names,
//# this will not be too expensive.
void TableParseSelect::handleColumn (Int stringType,
                                     const String& name,
                                     const TableExprNode& expr,
                                     const String& newName,
                                     const String& newNameMask,
                                     const String& newDtype)
{
  if (expr.isNull()  &&  stringType >= 0) {
    // A wildcarded column name is given.
    handleWildColumn (stringType, name);
  } else {
    // A single column is given.
    Int nrcol = columnNames_p.size();
    columnNames_p.resize     (nrcol+1);
    columnNameMasks_p.resize (nrcol+1);
    columnExpr_p.resize      (nrcol+1);
    columnOldNames_p.resize  (nrcol+1);
    columnDtypes_p.resize    (nrcol+1);
    columnKeywords_p.resize  (nrcol+1);
    if (expr.isNull()) {
      // A true column name is given.
      String oldName;
      String str = name;
      Int inx = str.index('.');
      if (inx < 0) {
        oldName = str;
      } else {
        oldName = str.after(inx);
      }
      // Make an expression of the column or keyword name.
      columnExpr_p[nrcol] = handleKeyCol (str, True);
      if (columnExpr_p[nrcol].table().isNull()) {
        // A keyword was given which is returned as a constant.
        nrSelExprUsed_p++;
      } else {
        // If a data type or shorthand is given, the column must be handled
        // as an expression.
        // The same is true if the same column is already used. In such a case
        // the user likely wants to duplicate the column with a different name.
        columnOldNames_p[nrcol] = oldName;
        if (!newDtype.empty()  ||  inx >= 0) {
          nrSelExprUsed_p++;
        } else {
          for (Int i=0; i<nrcol; ++i) {
            if (str == columnOldNames_p[i]) {
              nrSelExprUsed_p++;
              break;
            }
          }
        }
        // Get the keywords for this column (to copy unit, etc.)
        TableColumn tabcol(columnExpr_p[nrcol].table(), oldName);
        columnKeywords_p[nrcol] = tabcol.keywordSet();
      }
    } else {
      // An expression is given.
      columnExpr_p[nrcol] = expr;
      nrSelExprUsed_p++;
    }
    columnDtypes_p[nrcol]    = newDtype;
    columnNames_p[nrcol]     = newName;
    columnNameMasks_p[nrcol] = newNameMask;
    if (newName.empty()) {
      columnNames_p[nrcol] = columnOldNames_p[nrcol];
    }
  }
}

//# Handle a wildcarded a column name.
//# Add or remove to/from the block of column names as needed.
void TableParseSelect::handleWildColumn (Int stringType, const String& name)
{
  Int nrcol  = columnNames_p.size();
  String str = name.substr(2, name.size()-3);    // remove delimiters
  Bool caseInsensitive = ((stringType & 1) != 0);
  Bool negate          = ((stringType & 2) != 0);
  Regex regex;
  int shInx = -1;
  // See if the wildcarded name has a table shorthand in it.
  String shorthand;
  if (name[0] == 'p') {
    if (!negate) {
      shInx = str.index('.');
      if (shInx >= 0) {
        shorthand = str.before(shInx);
        str       = str.after(shInx);
      }
    }
    regex = Regex::fromPattern (str);
  } else {
    if (!negate) {
      shInx = str.index("\\.");
      if (shInx >= 0) {
        shorthand = str.before(shInx);
        str       = str.after(shInx+1);
      }
    }
    if (name[0] == 'f') {
      regex = Regex(str);
    } else {
      // For regex type m prepend and append .* unless begin or end regex is given.
      if (str.size() > 0  &&  str[0] != '^') {
        str = ".*" + str;
      }
      if (str.size() > 0  &&  str[str.size()-1] != '$') {
        str = str + ".*";
      }
      regex = Regex(str);
    }
  }
  if (!negate) {
    // Find all matching columns.
    Table tab = findTable(shorthand, False);
    if (tab.isNull()) {
      throw TableInvExpr("Shorthand " + shorthand + " in wildcarded column " +
                         name + " not defined in FROM clause");
    }
    Vector<String> columns = tab.tableDesc().columnNames();
    // Add back the delimiting . if a shorthand is given.
    if (shInx >= 0) {
      shorthand += '.';
    }
    Int nr = 0;
    for (uInt i=0; i<columns.size(); ++i) {
      String col = columns[i];
      if (caseInsensitive) {
        col.downcase();
      }
      if (col.matches(regex)) {
        ++nr;
      } else {
        columns[i] = String();
      }
    }
    // Add them to the list of column names.
    columnNames_p.resize     (nrcol+nr);
    columnNameMasks_p.resize (nrcol+nr);
    columnExpr_p.resize      (nrcol+nr);
    columnOldNames_p.resize  (nrcol+nr);
    columnDtypes_p.resize    (nrcol+nr);
    columnKeywords_p.resize  (nrcol+nr);
    for (uInt i=0; i<columns.size(); ++i) {
      if (! columns[i].empty()) {
        // Add the shorthand to the name, so negation takes that into account.
        columnNames_p[nrcol++] = shorthand + columns[i];
      }
    }
  } else {
    // Negation of wildcard, thus remove columns if matching.
    // If the negated wildcard is the first one, assume * was given before it.
    if (nrcol == 0) {
      handleWildColumn (0, "p/*/");
      nrcol = columnNames_p.size();
    }
    // This is done until the last non-wildcarded column name.
    while (nrcol > 0) {
      --nrcol;
      if (! columnExpr_p[nrcol].isNull()) {
        break;
      }
      String col = columnNames_p[nrcol];
      if (!col.empty()) {
        if (caseInsensitive) {
          col.downcase();
        }
        if (col.matches(regex)) {
          columnNames_p[nrcol] = String();
        }
      }
    }
  }
}

//# Finish the additions to the block of column names
//# by removing the deleted empty names and creating Expr objects as needed.
void TableParseSelect::handleColumnFinish (Bool distinct)
{
  distinct_p = distinct;
  // Remove the deleted column names.
  // Create Expr objects for the wildcarded names.
  Int nrcol = columnNames_p.size();
  if (nrcol > 0) {
    if (resultSet_p != 0) {
      throw TableInvExpr("Expressions can be given in SELECT or GIVING, "
                         "not both");
    }
    Block<String> names(nrcol);
    Block<String> nameMasks(nrcol);
    Block<String> oldNames(nrcol);
    Block<TableExprNode> exprs(nrcol);
    Block<String> dtypes(nrcol);
    Block<TableRecord> keywords(nrcol);
    Int nr = 0;
    for (Int i=0; i<nrcol; ++i) {
      if (! (columnExpr_p[i].isNull()  &&  columnNames_p[i].empty())) {
        names[nr]     = columnNames_p[i];
        nameMasks[nr] = columnNameMasks_p[i];
        oldNames[nr]  = columnOldNames_p[i];
        exprs[nr]     = columnExpr_p[i];
        dtypes[nr]    = columnDtypes_p[i];
        keywords[nr]  = columnKeywords_p[i];
        // Create an Expr object if needed.
        if (exprs[nr].isNull()) {
          // That can only be the case if no old name is filled in.
          AlwaysAssert (oldNames[nr].empty(), AipsError);
          String name = names[nr];
          Int j = name.index('.');
          if (j >= 0) {
            name = name.after(j);
          }
          // Make an expression of the column name.
          exprs[nr]    = handleKeyCol (name, False);
          names[nr]    = name;
          oldNames[nr] = name;
          // Get the keywords for this column (to copy unit, etc.)
          TableColumn tabcol(exprs[nr].table(), name);
          keywords[nr] = tabcol.keywordSet();
        }
        ++nr;
      }
    }
    names.resize    (nr, True);
    oldNames.resize (nr, True);
    exprs.resize    (nr, True);
    dtypes.resize   (nr, True);
    keywords.resize (nr, True);
    columnNames_p     = names;
    columnNameMasks_p = nameMasks;
    columnOldNames_p  = oldNames;
    columnExpr_p      = exprs;
    columnDtypes_p    = dtypes;
    columnKeywords_p  = keywords;
  }
  if (distinct_p  &&  columnNames_p.size() == 0) {
    throw TableInvExpr ("SELECT DISTINCT can only be given with at least "
                        "one column name");
  }
  // Make (empty) new table if select expressions were given.
  // This table is used when output columns are used in ORDERBY or HAVING.
  if (nrSelExprUsed_p > 0) {
    makeProjectExprTable();
  }
}

Table TableParseSelect::createTable (const TableDesc& td,
                                     Int64 nrow, const Record& dmInfo,
                                     const std::vector<const Table*>& tempTables,
                                     const std::vector<TableParseSelect*>& stack)
{
  // If the table name contains ::, a subtable has to be created.
  // Split the name at the last ::.
  Vector<String> parts = stringToVector(resultName_p, std::regex("::"));
  if (parts.size() > 1) {
    return createSubTable (parts[parts.size()-1], td, nrow, dmInfo,
                           tempTables, stack);
  } 
  // Create the table.
  // The types are defined in function handleGiving.
  Table::TableType   ttype = Table::Plain;
  Table::TableOption topt  = Table::New;
  if (!overwrite_p)  topt  = Table::NewNoReplace;
  // Use default Memory if no name or 'memory' has been given.
  if (resultName_p.empty()) {
    ttype = Table::Memory;
  } else if (resultType_p == 1) {
    ttype = Table::Memory;
  } else if (resultType_p == 2) {
    topt  = Table::Scratch;
  }
  SetupNewTable newtab(resultName_p, td, topt, storageOption_p);
  newtab.bindCreate (dmInfo);
  Table tab(newtab, ttype, nrow, False, endianFormat_p);
  resultCreated_p = True;
  return tab;
}

Table TableParseSelect::openParentTable (const String& fullName,
                                         const String& subTableName,
                                         const std::vector<const Table*>& tempTables,
                                         const std::vector<TableParseSelect*>& stack)
{
  // Remove ::subtableName from the full table name to get the parent's name.
  String tableName (fullName.substr(0,
                                    fullName.size() - subTableName.size() - 2));
  // Open the parent table.
  Table parent = getTable (-1, tableName, Table(), tempTables, stack, True);
  // Create the subtable and define the keyword in the parent referring it.
  String parentName = parent.tableName();
  if (parentName.empty()) {
    throw TableError("Parent table in " + resultName_p + " seems to be transient");
  }
  return parent;
}

Table TableParseSelect::createSubTable (const String& subtableName,
                                        const TableDesc& td, Int64 nrow,
                                        const Record& dmInfo,
                                        const std::vector<const Table*>& tempTables,
                                        const std::vector<TableParseSelect*>& stack)
{
  Table parent (openParentTable(resultName_p, subtableName, tempTables, stack));
  return TableUtil::createSubTable (parent, subtableName, td,
                                    overwrite_p ? Table::New : Table::NewNoReplace,
                                    storageOption_p, dmInfo, TableLock(),
                                    nrow, False, endianFormat_p, TSMOption());
}

void TableParseSelect::makeProjectExprTable()
{
  // Make a column description for all expressions.
  // Check if all tables involved have the same nr of rows as the first one.
  TableDesc td;
  for (uInt i=0; i<columnExpr_p.size(); i++) {
    // If no new name is given, make one (unique).
    String newName = columnNames_p[i];
    if (newName.empty()) {
      String nm = "Col_" + String::toString(i+1);
      Int seqnr = 0;
      newName = nm;
      Bool unique = False;
      while (!unique) {
        unique = True;
        for (uInt i=0; i<columnNames_p.size(); i++) {
          if (newName == columnNames_p[i]) {
            unique = False;
            seqnr++;
            newName = nm + "_" + String::toString(seqnr);
            break;
          }
        }
      }
      columnNames_p[i] = newName;
    }
    DataType dtype = makeDataType (columnExpr_p[i].dataType(),
                                   columnDtypes_p[i], columnNames_p[i]);
    addColumnDesc (td, dtype, newName, 0,
                   columnExpr_p[i].isScalar() ? -1:0,    //ndim
                   IPosition(), "", "", "",
                   columnKeywords_p[i],
                   Vector<String>(1, columnExpr_p[i].unit().getName()),
                   columnExpr_p[i].attributes());
    if (! columnNameMasks_p[i].empty()) {
      addColumnDesc (td, TpBool, columnNameMasks_p[i], 0,
                     columnExpr_p[i].isScalar() ? -1:0,    //ndim
                     IPosition(), "", "", "",
                     TableRecord(), Vector<String>(), Record());
    }
  }
  // Create the table.
  projectExprTable_p = createTable (td, 0, dminfo_p,
                                    std::vector<const Table*>(),
                                    std::vector<TableParseSelect*>());
}

void TableParseSelect::makeProjectExprSel()
{
  // Create/initialize the block of indices of projected columns used
  // elsewhere.
  projectExprSelColumn_p.resize (columnNames_p.size());
  std::fill (projectExprSelColumn_p.begin(),
             projectExprSelColumn_p.end(), False);
  // Set to True for the used columns.
  uInt ncol = 0;
  for (uInt i=0; i<projectExprSubset_p.size(); ++i) {
    AlwaysAssert (projectExprSubset_p[i] < projectExprSelColumn_p.size(),
                  AipsError);
    if (! projectExprSelColumn_p[projectExprSubset_p[i]]) {
      projectExprSelColumn_p[projectExprSubset_p[i]] = True;
      ncol++;
    }
  }
  // Resize the subset vector. It is not really used anymore, but the
  // tracing shows its size as the nr of pre-projected columns.
  projectExprSubset_p.resize (ncol, True);
}

//# Add a column specification.
void TableParseSelect::handleColSpec (const String& colName,
                                      const String& likeColName,
                                      const String& dtstr,
                                      const Record& spec,
                                      Bool isCOrder)
{
  // Check if specific column info is given.
  DataType dtype = TpOther;
  Int options = 0;
  Int ndim = -1;
  IPosition shape;
  String dmType;
  String dmGroup;
  String comment;
  Vector<String> unit;
  TableRecord keywords;
  // See if the column is like another column.
  if (likeColName.empty()) {
    AlwaysAssert (! dtstr.empty(), AipsError);
  } else {
    // Use the description of the LIKE column.
    std::pair<ColumnDesc,Record> cdr = findColumnInfo (likeColName, colName);
    const ColumnDesc& cd = cdr.first;
    dtype = cd.dataType();
    options = cd.options();
    if (cd.isArray()) {
      ndim = cd.ndim();
    }
    shape = cd.shape();
    dmType = cd.dataManagerType();
    dmGroup = cd.dataManagerGroup();
    comment = cd.comment();
    keywords = cd.keywordSet();
    if (keywords.isDefined ("QuantumUnits")) {
      unit.reference (cd.keywordSet().asArrayString ("QuantumUnits"));
    }
    // Merge its dminfo into the overall one.
    DataManInfo::mergeInfo (dminfo_p, cdr.second);
  }
  if (! dtstr.empty()) {
    dtype = makeDataType (TpOther, dtstr, colName);
  }
  // Get the possible specifications (which override the LIKE column).
  for (uInt i=0; i<spec.nfields(); i++) {
    String name = spec.name(i);
    name.upcase();
    if (name == "NDIM") {
      ndim = spec.asInt(i);
    } else if (name == "SHAPE") {
      Vector<Int> ivec(spec.toArrayInt(i));
      Int nd = ivec.size();
      shape.resize (nd);
      if (isCOrder) {
        for (Int i=0; i<nd; ++i) {
          shape[i] = ivec[nd-i-1];
        }
      } else {
        shape = IPosition(ivec);
      }
      if (ndim < 0) {
        ndim = 0;
      }
    } else if (name == "DIRECT") {
      if (spec.asInt(i) == 1) {
        options = 1;
      }
    } else if (name == "DMTYPE") {
      dmType = spec.asString(i);
    } else if (name == "DMGROUP") {
      dmGroup = spec.asString(i);
    } else if (name == "COMMENT") {
      comment = spec.asString(i);
    } else if (name == "UNIT") {
      if (spec.dataType(i) == TpString) {
        unit.reference (Vector<String>(1, spec.asString(i)));
      } else {
        unit.reference (spec.asArrayString(i));
      }
    } else {
      throw TableError ("TableParseSelect::handleColSpec - "
                        "column specification field name " + name +
                        " is unknown");
    }
  }
  // Now add the scalar or array column description.
  addColumnDesc (*tableDesc_p, dtype, colName, options, ndim, shape,
                 dmType, dmGroup, comment, keywords, unit, Record());
  Int nrcol = columnNames_p.size();
  columnNames_p.resize (nrcol+1);
  columnNames_p[nrcol] = colName;
}

void TableParseSelect::handleGroupby (const std::vector<TableExprNode>& nodes,
                                      Bool rollup)
{
  groupbyNodes_p  = nodes;
  groupbyRollup_p = rollup;
  if (rollup) {
    throw TableInvExpr ("ROLLUP is not supported yet in the GROUPBY");
  }
  for (uInt i=0; i<nodes.size(); ++i) {
    checkAggrFuncs (nodes[i]);
    if (! nodes[i].isScalar()) {
      throw TableInvExpr("GROUPBY column/expression must be a scalar");
    }
  }
}

void TableParseSelect::handleHaving (const TableExprNode& node)
{
  havingNode_p = node;
  if (node.dataType() != TpBool  ||  !node.isScalar()) {
    throw TableInvExpr ("HAVING expression must result in a bool scalar value");
  }
}

void TableParseSelect::handleDropTab(const std::vector<const Table*>& tempTables,
                                     const std::vector<TableParseSelect*>& stack)
{
  // Delete all tables. It has already been checked they exist.
  for (TableParse& tab : fromTables_p) {
    // Split the name on :: to see if a subtable has to be deleted.
    Vector<String> parts = stringToVector(tab.name(), std::regex("::"));
    if (parts.size() > 1) {
      // There is a subtable, so delete the keyword in its parent.
      // First get the size of the parent name.
      const String& subName(parts[parts.size() - 1]);
      size_t sz = tab.name().size() - subName.size() - 2;
      Table parent(getTable (tab.tabnr(), tab.name().substr(0,sz),
                              Table(), tempTables, stack));
      // Make sure subtable is closed, otherwise cannot be deleted.
      tab.table() = Table();
      TableUtil::deleteSubTable (parent, subName);
    } else {
      tab.table().markForDelete();
    }
  }
}

void TableParseSelect::handleCreTab (const Record& dmInfo,
                                     const std::vector<const Table*>& tempTables,
                                     const std::vector<TableParseSelect*>& stack)
{
  DataManInfo::mergeInfo (dminfo_p, dmInfo);
  DataManInfo::finalizeMerge (*tableDesc_p, dminfo_p);
  table_p = createTable (*tableDesc_p, limit_p, dminfo_p, tempTables, stack);
}

void TableParseSelect::handleAltTab()
{
  // The first table has to be altered.
  AlwaysAssert (fromTables_p.size() > 0, AipsError);
  table_p = fromTables_p[0].table();
  table_p.reopenRW();
  if (! table_p.isWritable()) {
    throw TableInvExpr ("Table " + table_p.tableName() + " is not writable");
  }
}

void TableParseSelect::handleAddCol (const Record& dmInfo)
{
  // Merge the given dminfo into the dataman-info of the columns.
  DataManInfo::mergeInfo (dminfo_p, dmInfo);
  DataManInfo::finalizeMerge (*tableDesc_p, dminfo_p);
  DataManInfo::adaptNames (dminfo_p, table_p);
  if (dminfo_p.empty()) {
    StandardStMan ssm;
    table_p.addColumn (*tableDesc_p, ssm);
  } else {
    table_p.addColumn (*tableDesc_p, dminfo_p);
  }
}

void TableParseSelect::initDescriptions (const TableDesc& desc,
                                         const Record& dminfo)
{
  tableDesc_p = std::shared_ptr<TableDesc>(new TableDesc(desc));
  dminfo_p    = dminfo;
}

ValueHolder TableParseSelect::getRecFld (const String& name)
{
  String keyName;
  const TableRecord& keyset = findKeyword (name, keyName, False);
  Int fieldnr = keyset.fieldNumber (keyName);
  if (fieldnr < 0) {
    throw (TableInvExpr ("Keyword " + name + " does not exist"));
  }
  return keyset.asValueHolder (fieldnr);
}

void TableParseSelect::setRecFld (RecordInterface& rec,
                                  const String& name,
                                  const String& dtype,
                                  const ValueHolder& vh)
{
  String type = getTypeString (dtype, vh.dataType());
  if (isScalar(vh.dataType())) {
    if (type == "B") {
      rec.define (name, vh.asBool());
    } else if (type == "U1") {
      rec.define (name, vh.asuChar());
    } else if (type == "U4") {
      rec.define (name, vh.asuInt());
    } else if (type == "I2") {
      rec.define (name, vh.asShort());
    } else if (type == "I4") {
      rec.define (name, vh.asInt());
    } else if (type == "I8") {
      rec.define (name, vh.asInt64());
    } else if (type == "R4") {
      rec.define (name, vh.asFloat());
    } else if (type == "R8") {
      rec.define (name, vh.asDouble());
    } else if (type == "C4") {
      rec.define (name, vh.asComplex());
    } else if (type == "C8") {
      rec.define (name, vh.asDComplex());
    } else if (type == "S") {
      rec.define (name, vh.asString());
    } else {
      throw TableInvExpr ("TableParse::setRecFld - "
                          "unknown data type " + type);
    }
  } else {
    if (type == "B") {
      rec.define (name, vh.asArrayBool());
    } else if (type == "U1") {
      rec.define (name, vh.asArrayuChar());
    } else if (type == "U4") {
      rec.define (name, vh.asArrayuInt());
    } else if (type == "I2") {
      rec.define (name, vh.asArrayShort());
    } else if (type == "I4") {
      rec.define (name, vh.asArrayInt());
    } else if (type == "I8") {
      rec.define (name, vh.asArrayInt64());
    } else if (type == "R4") {
      rec.define (name, vh.asArrayFloat());
    } else if (type == "R8") {
      rec.define (name, vh.asArrayDouble());
    } else if (type == "C4") {
      rec.define (name, vh.asArrayComplex());
    } else if (type == "C8") {
      rec.define (name, vh.asArrayDComplex());
    } else if (type == "S") {
      rec.define (name, vh.asArrayString());
    } else {
      throw TableInvExpr ("TableParse::setRecFld - "
                          "unknown data type " + type);
    }
  }
}

String TableParseSelect::getTypeString (const String& typeStr, DataType type)
{
  String out = typeStr;
  if (out.empty()) {
    switch (type) {
    case TpBool:
    case TpArrayBool:
      out = "B";
      break;
    case TpUChar:
    case TpArrayUChar:
      out = "U1";
      break;
    case TpUShort:
    case TpArrayUShort:
      out = "U2";          // github.com/ICRAR/skuareview
      break;
    case TpUInt:
    case TpArrayUInt:
      out = "U4";
      break;
    case TpShort:
    case TpArrayShort:
      out = "I2";
      break;
    case TpInt:
    case TpArrayInt:
      out = "I4";
      break;
    case TpInt64:
    case TpArrayInt64:
      out = "I8";
      break;
    case TpFloat:
    case TpArrayFloat:
      out = "R4";
      break;
    case TpDouble:
    case TpArrayDouble:
      out = "R8";
      break;
    case TpComplex:
    case TpArrayComplex:
      out = "C4";
      break;
    case TpDComplex:
    case TpArrayDComplex:
      out = "C8";
      break;
    case TpString:
    case TpArrayString:
      out = "S";
      break;
    default:
      throw TableInvExpr ("TableParse::getTypeString - "
                          "value has an unknown data type " +
                          String::toString(type));
    }
  }
  return out;
}

TableRecord& TableParseSelect::findKeyword (const String& name,
                                            String& keyName,
                                            Bool update)
{
  //# Split the name into optional shorthand, column, and keyword.
  String shand, columnName;
  Vector<String> fieldNames;
  splitName (shand, columnName, fieldNames, name, True, True, False);
  Table tab = findTable (shand, False);
  if (tab.isNull()) {
    throw (TableInvExpr("Shorthand " + shand + " not defined in FROM clause"));
  }
  TableRecord* rec;
  String fullName;
  if (columnName.empty()) {
    if (update) {
      rec = TableExprNode::findLastKeyRec (tab.rwKeywordSet(),
                                           fieldNames, fullName);
    } else {
      rec = TableExprNode::findLastKeyRec (tab.keywordSet(),
                                           fieldNames, fullName);
    }
  } else {
    if (update) {
      TableRecord& colkeys (TableColumn(tab, columnName).rwKeywordSet());
      rec = TableExprNode::findLastKeyRec (colkeys, fieldNames, fullName);
    } else {
      const TableRecord& colkeys (TableColumn(tab, columnName).keywordSet());
      rec = TableExprNode::findLastKeyRec (colkeys, fieldNames, fullName);
    }
  }
  keyName = fieldNames[fieldNames.size() -1 ];
  return *rec;
}

void TableParseSelect::handleSetKey (const String& name,
                                     const String& dtype,
                                     const ValueHolder& value)
{
  String keyName;
  TableRecord& keyset = findKeyword (name, keyName);
  if (value.dataType() == TpString  ||  value.dataType() == TpRecord) {
    keyset.defineFromValueHolder (keyName, value);
  } else {
    setRecFld (keyset, keyName, dtype, value);
  }
}

void TableParseSelect::handleCopyCol (Bool showTimings)
{
  // Note that table_p, tableDesc_p and dminfo_p have already been set.
  Timer timer;
  handleAddCol (Record());
  doUpdate (False, Table(), table_p, table_p.rowNumbers());
  if (showTimings) {
    timer.show ("  Copy Column ");
  }
}

void TableParseSelect::handleRenameKey (const String& oldName,
                                        const String& newName)
{
  String keyName;
  TableRecord& keyset = findKeyword (oldName, keyName);
  keyset.renameField (newName, keyName);
}

void TableParseSelect::handleRemoveKey (const String& name)
{
  String keyName;
  TableRecord& keyset = findKeyword (name, keyName);
  keyset.removeField (keyName);
}

void TableParseSelect::handleWhere (const TableExprNode& node)
{
  checkAggrFuncs (node);
  node_p = node;
}

void TableParseSelect::handleSort (const std::vector<TableParseSort>& sort,
                                   Bool noDuplicates,
                                   Sort::Order order)
{
  noDupl_p = noDuplicates;
  order_p  = order;
  sort_p   = sort;
}

void TableParseSelect::handleCalcComm (const TableExprNode& node)
{
  checkAggrFuncs (node);
  node_p = node;
}

Block<String> TableParseSelect::getStoredColumns (const Table& tab) const
{
  Block<String> names;
  const TableDesc& tdesc = tab.tableDesc();
  for (uInt i=0; i<tdesc.ncolumn(); i++) {
    const String& colnm = tdesc[i].name();
    if (tab.isColumnStored(colnm)) {
      uInt inx = names.size();
      names.resize (inx + 1);
      names[inx] = colnm;
    }
  }
  return names;
}

//# Execute a query in the FROM clause and return the resulting table.
Table TableParseSelect::doFromQuery (Bool showTimings)
{
  Timer timer;
  // Execute the nested command.
  execute (False, False, True, 0);
  if (showTimings) {
    timer.show ("  From query  ");
  }
  return table_p;
}

//# Execute a subquery for an EXISTS operator.
TableExprNode TableParseSelect::doExists (Bool notexists, Bool showTimings)
{
  Timer timer;
  // Execute the nested command.
  // Default limit_p is 1.
  execute (False, True, True, 1);
  if (showTimings) {
    timer.show ("  Exists query");
  }
  // Flag notexists tells if NOT EXISTS or EXISTS was given.
  return TableExprNode (notexists == (Int64(table_p.nrow()) < limit_p));
}

//# Execute a subquery and create the correct node object for it.
TableExprNode TableParseSelect::doSubQuery (Bool showTimings)
{
  Timer timer;
  // Execute the nested command.
  execute (False, True, True, 0);
  TableExprNode result;
  if (resultSet_p != 0) {
    // A set specification was given, so make the set.
    result = makeSubSet();
  } else {
    // A single column was given, so get its data.
    result = getColSet();
  }
  if (showTimings) {
    timer.show ("  Subquery    ");
  }
  return result;
}

TableExprNode TableParseSelect::getColSet()
{
  // Check if there is only one column.
  const TableDesc& tableDesc = table_p.tableDesc();
  if (tableDesc.ncolumn() != 1) {
    throw (TableInvExpr ("Nested query should select 1 column"));
  }
  const ColumnDesc& colDesc = tableDesc.columnDesc(0);
  TableColumn tabcol (table_p, colDesc.name());
  TENShPtr tsnptr;
  if (colDesc.isScalar()) {
    switch (colDesc.dataType()) {
    case TpBool:
      tsnptr = new TableExprNodeArrayConstBool
        (ScalarColumn<Bool>(tabcol).getColumn());
      break;
    case TpUChar:
      tsnptr = new TableExprNodeArrayConstInt
        (ScalarColumn<uChar>(tabcol).getColumn());
      break;
    case TpShort:
      tsnptr = new TableExprNodeArrayConstInt
        (ScalarColumn<Short>(tabcol).getColumn());
      break;
    case TpUShort:
      tsnptr = new TableExprNodeArrayConstInt
        (ScalarColumn<uShort>(tabcol).getColumn());
      break;
    case TpInt:
      tsnptr = new TableExprNodeArrayConstInt
        (ScalarColumn<Int>(tabcol).getColumn());
      break;
    case TpUInt:
      tsnptr = new TableExprNodeArrayConstInt
        (ScalarColumn<uInt>(tabcol).getColumn());
      break;
    case TpInt64:
      tsnptr = new TableExprNodeArrayConstInt
        (ScalarColumn<Int64>(tabcol).getColumn());
      break;
    case TpFloat:
      tsnptr = new TableExprNodeArrayConstDouble
        (ScalarColumn<Float>(tabcol).getColumn());
      break;
    case TpDouble:
      tsnptr = new TableExprNodeArrayConstDouble
        (ScalarColumn<Double>(tabcol).getColumn());
      break;
    case TpComplex:
      tsnptr = new TableExprNodeArrayConstDComplex
        (ScalarColumn<Complex>(tabcol).getColumn());
      break;
    case TpDComplex:
      tsnptr = new TableExprNodeArrayConstDComplex
        (ScalarColumn<DComplex>(tabcol).getColumn());
      break;
    case TpString:
      tsnptr = new TableExprNodeArrayConstString
        (ScalarColumn<String>(tabcol).getColumn());
      break;
    default:
      throw (TableInvExpr ("Nested query column " + colDesc.name() +
                           " has unknown data type"));
    }
  } else {
    switch (colDesc.dataType()) {
    case TpBool:
      tsnptr = new TableExprNodeArrayConstBool
        (ArrayColumn<Bool>(tabcol).getColumn());
      break;
    case TpUChar:
      tsnptr = new TableExprNodeArrayConstInt
        (ArrayColumn<uChar>(tabcol).getColumn());
      break;
    case TpShort:
      tsnptr = new TableExprNodeArrayConstInt
        (ArrayColumn<Short>(tabcol).getColumn());
      break;
    case TpUShort:
      tsnptr = new TableExprNodeArrayConstInt
        (ArrayColumn<uShort>(tabcol).getColumn());
      break;
    case TpInt:
      tsnptr = new TableExprNodeArrayConstInt
        (ArrayColumn<Int>(tabcol).getColumn());
      break;
    case TpUInt:
      tsnptr = new TableExprNodeArrayConstInt
        (ArrayColumn<uInt>(tabcol).getColumn());
      break;
    case TpInt64:
      tsnptr = new TableExprNodeArrayConstInt
        (ArrayColumn<Int64>(tabcol).getColumn());
      break;
    case TpFloat:
      tsnptr = new TableExprNodeArrayConstDouble
        (ArrayColumn<Float>(tabcol).getColumn());
      break;
    case TpDouble:
      tsnptr = new TableExprNodeArrayConstDouble
        (ArrayColumn<Double>(tabcol).getColumn());
      break;
    case TpComplex:
      tsnptr = new TableExprNodeArrayConstDComplex
        (ArrayColumn<Complex>(tabcol).getColumn());
      break;
    case TpDComplex:
      tsnptr = new TableExprNodeArrayConstDComplex
        (ArrayColumn<DComplex>(tabcol).getColumn());
      break;
    case TpString:
      tsnptr = new TableExprNodeArrayConstString
        (ArrayColumn<String>(tabcol).getColumn());
      break;
    default:
      throw (TableInvExpr ("Nested query column " + colDesc.name() +
                           " has unknown data type"));
    }
  }
  //# Fill in the column unit (if defined).
  tsnptr->setUnit (TableExprNodeColumn::getColumnUnit (tabcol));
  return tsnptr;
}


TableExprNode TableParseSelect::makeSubSet() const
{
  // Perform some checks on the given set.
  if (resultSet_p->hasArrays()) {
    throw (TableInvExpr ("Set in GIVING clause should contain scalar"
                         " elements"));
  }
  resultSet_p->checkEqualDataTypes();
  // Link to set to make sure that TableExprNode hereafter does not delete
  // the object.
  TableExprNodeSet set(rownrs_p, *resultSet_p);
  return set.setOrArray();
}

void TableParseSelect::handleLimit (const TableExprNodeSetElem& expr)
{
  if (expr.start()) {
    offset_p = evalIntScaExpr (TableExprNode(expr.start()));
  }
  if (expr.increment()) {
    stride_p = evalIntScaExpr (TableExprNode(expr.increment()));
    if (stride_p <= 0) {
      throw TableInvExpr ("in the LIMIT clause stride " +
                          String::toString(stride_p) +
                          " must be positive");
    }
  }
  if (expr.end()) {
    endrow_p = evalIntScaExpr (TableExprNode(expr.end()));
  }
}

void TableParseSelect::handleLimit (const TableExprNode& expr)
{
  limit_p = evalIntScaExpr (expr);
}

void TableParseSelect::handleOffset (const TableExprNode& expr)
{
  offset_p = evalIntScaExpr (expr);
}

void TableParseSelect::makeTableNoFrom (const std::vector<TableParseSelect*>& stack)
{
  if (limit_p < 0  ||  offset_p < 0  ||  endrow_p < 0) {
    throw TableInvExpr("LIMIT and OFFSET values cannot be negative if no "
                       "tables are given in the FROM clause");
  }
  // Use 1 row if limit_p nor endrow_p is given.
  Int64 nrow = 1;
  if (limit_p > 0) {
    nrow = limit_p + offset_p;
  } else if (endrow_p > 0) {
    nrow = endrow_p;
  }
  // Add a temp table with no columns and some rows to the FROM list.
  Table tab(Table::Memory);
  tab.addRow(nrow);
  addTable (-1, String(), tab, String(), True, std::vector<const Table*>(), stack);
}

void TableParseSelect::handleAddRow (const TableExprNode& expr)
{
  table_p.addRow (evalIntScaExpr (expr));
}

Int64 TableParseSelect::evalIntScaExpr (const TableExprNode& expr) const
{
  checkAggrFuncs (expr);
  if (!expr.table().isNull()) {
    throw TableInvExpr ("LIMIT or OFFSET expression cannot contain columns");
  }
  // Get the value as a double, because some expressions result in double.
  // Round it to an integer.
  TableExprId rowid(0);
  Double val;
  expr.get (rowid, val);
  if (val >= 0) {
    return static_cast<Int64>(val+0.5);
  }
  return -static_cast<Int64>(-val+0.5);
}

void TableParseSelect::handleUpdate()
{
  columnNames_p.resize (update_p.size());
  for (uInt i=0; i<update_p.size(); i++) {
    columnNames_p[i] = update_p[i]->columnName();
  }
}

void TableParseSelect::handleInsert()
{
  // If no columns were given, all stored columns in the first table
  // are the target columns.
  if (columnNames_p.size() == 0) {
    columnNames_p = getStoredColumns (fromTables_p[0].table());
    columnNameMasks_p.resize (columnNames_p.size());
  }
  // Check if #columns and values match.
  // Copy the names to the update objects.
  if (update_p.size() != columnNames_p.size()) {
    throw TableInvExpr ("Error in INSERT command; nr of columns (=" +
                        String::toString(columnNames_p.size()) +
                        ") mismatches "
                        "number of VALUES expressions (=" +
                        String::toString(Int(update_p.size())) + ")");
  }
  for (uInt i=0; i<update_p.size(); i++) {
    update_p[i]->setColumnName     (columnNames_p[i]);
    update_p[i]->setColumnNameMask (columnNameMasks_p[i]);
  }
}

void TableParseSelect::handleInsert (TableParseSelect* sel)
{
  insSel_p = sel;
}

void TableParseSelect::handleCount()
{
  if (columnExpr_p.size() == 0) {
    throw TableInvExpr ("No COUNT columns given");
  }
  for (uInt i=0; i<columnExpr_p.size(); i++) {
    checkAggrFuncs (columnExpr_p[i]);
    if (!columnExpr_p[i].isScalar()) {
      throw TableInvExpr ("COUNT column " + columnNames_p[i] + " is not scalar");
    }
  }
}

//# Execute the updates.
void TableParseSelect::doUpdate (Bool showTimings, const Table& origTable,
                                 Table& updTable, const Vector<rownr_t>& rownrs,
                                 const CountedPtr<TableExprGroupResult>& groups)
{
  Timer timer;
  AlwaysAssert (updTable.nrow() == rownrs.size(), AipsError);
  //# If no rows to be updated, return immediately.
  //# (the code below will fail for empty tables)
  if (rownrs.empty()) {
    return;
  }
  // Reopen the table for write.
  updTable.reopenRW();
  if (! updTable.isWritable()) {
    throw TableInvExpr ("Table " + updTable.tableName() + " is not writable");
  }
  //# First check if the update columns and values are correct.
  const TableDesc& tabdesc = updTable.tableDesc();
  uInt nrkey = update_p.size();
  Block<TableColumn> cols(nrkey);
  Block<ArrayColumn<Bool> > maskCols(nrkey);
  Block<Int> dtypeCol(nrkey);
  Block<Bool> isScalarCol(nrkey);
  for (uInt i=0; i<nrkey; i++) {
    TableParseUpdate& key = *(update_p[i]);
    const String& colName = key.columnName();
    const String& colNameMask = key.columnNameMask();
    //# Check if the correct table is used in the update and index expression.
    //# A constant expression can be given.
    if (! key.node().checkTableSize (origTable, True)) {
      throw TableInvExpr ("Table(s) with incorrect size used in the "
                          "UPDATE expr of column " + colName +
                          " (mismatches first table)");
    }
    if (key.indexPtr() != 0) {
      if (! key.indexNode().checkTableSize (updTable, True)) {
        throw TableInvExpr ("Table(s) with incorrect size used in the "
                            "index expr in UPDATE of column " + colName +
                            " (mismatches first table)");
      }
    }
    //# This throws an exception for unknown data types (datetime, regex).
    key.node().getColumnDataType();
    //# Check if the column exists and is writable.
    if (! tabdesc.isColumn (colName)) {
      throw TableInvExpr ("Update column " + colName +
                          " does not exist in table " +
                          updTable.tableName());
    }
    if (! updTable.isColumnWritable (colName)) {
      throw TableInvExpr ("Update column " + colName +
                          " is not writable in table " +
                          updTable.tableName());
    }
    const ColumnDesc& coldesc = tabdesc[colName];
    Bool isScalar = coldesc.isScalar();
    isScalarCol[i] = isScalar;
    if (! colNameMask.empty()) {
      if (! tabdesc.isColumn (colNameMask)) {
        throw TableInvExpr ("Update column " + colNameMask +
                            " does not exist in table " +
                            updTable.tableName());
      }
      if (! updTable.isColumnWritable (colNameMask)) {
        throw TableInvExpr ("Update column " + colNameMask +
                            " is not writable in table " +
                            updTable.tableName());
      }
      const ColumnDesc& coldescMask = tabdesc[colNameMask];
      if (key.node().isScalar()) {
        throw TableInvExpr ("Update mask column " + colNameMask +
                            " cannot be given for a scalar expression");
      }
      if (coldescMask.dataType() != TpBool) {
        throw TableInvExpr ("Update mask column " + colNameMask +
                            " must have data type Bool");
      }
      maskCols[i].attach (updTable, colNameMask);
    }
    //# An index expression can only be given for an array column.
    if (key.indexPtr() != 0) {
      if (isScalar) {
        throw TableInvExpr ("Index value cannot be given in UPDATE of "
                            " scalar column " + colName);
      }
      if (key.indexPtr()->isSingle()) {
        isScalar = True;
      }
    }
    //# Check if the value type matches.
    if (isScalar  &&  !key.node().isScalar()) {
      throw TableInvExpr ("An array value cannot be used in UPDATE of "
                          " scalar element of column " +
                          colName + " in table " +
                          updTable.tableName());
    }
    cols[i].attach (updTable, colName);
    dtypeCol[i] = coldesc.dataType();
    // If needed, make the expression's unit the same as the column unit.
    key.node().adaptUnit (TableExprNodeColumn::getColumnUnit
                                  (TableColumn(updTable, colName)));
  }
  // Loop through all rows in the table and update each row.
  TableExprIdAggr rowid(groups);
  for (rownr_t row=0; row<rownrs.size(); ++row) {
    rowid.setRownr (rownrs[row]);
    for (uInt i=0; i<nrkey; i++) {
      TableColumn& col = cols[i];
      const TableParseUpdate& key = *(update_p[i]);
      const TableExprNode& node = key.node();
      // Get possible subscripts.
      const Slicer* slicerPtr = 0;
      if (key.indexPtr() != 0) {
        slicerPtr = &(key.indexPtr()->getSlicer(rowid));
      }
      Bool isSca = isScalarCol[i];
      // Evaluate a possible mask.
      MArray<Bool> mask;
      if (! key.mask().isNull()) {
        key.mask().get (rowid, mask);
      }
      // The expression node type determines how to get the data.
      // The column data type determines how to put it.
      // The node data type should be convertible to the column data type.
      // The updateValue function does the actual work.
      // We simply switch on the types.
      switch (node.getNodeRep()->dataType()) {
      case TableExprNodeRep::NTBool:
        switch (dtypeCol[i]) {
        case TpBool:
          updateValue<Bool,Bool> (row, rowid, isSca, node, mask.array(), key.maskFirst(), col, slicerPtr, maskCols[i]);
          break;
        default:
          throw TableInvExpr ("Column " + update_p[i]->columnName() +
                              " has an invalid data type for an"
                              " UPDATE with a bool value");
        }
        break;

      case TableExprNodeRep::NTString:
        switch (dtypeCol[i]) {
        case TpString:
          updateValue<String,String> (row, rowid, isSca, node, mask.array(), key.maskFirst(), col, slicerPtr, maskCols[i]);
          break;
        default:
          throw TableInvExpr ("Column " + update_p[i]->columnName() +
                              " has an invalid data type for an"
                              " UPDATE with a string value");
        }
        break;

      case TableExprNodeRep::NTInt:
        switch (dtypeCol[i]) {
        case TpUChar:
          updateValue<uChar,Int64> (row, rowid, isSca, node, mask.array(), key.maskFirst(), col, slicerPtr, maskCols[i]);
           break;
        case TpShort:
          updateValue<Short,Int64> (row, rowid, isSca, node, mask.array(), key.maskFirst(), col, slicerPtr, maskCols[i]);
             break;
        case TpUShort:
          updateValue<uShort,Int64> (row, rowid, isSca, node, mask.array(), key.maskFirst(), col, slicerPtr, maskCols[i]);
           break;
        case TpInt:
          updateValue<Int,Int64> (row, rowid, isSca, node, mask.array(), key.maskFirst(), col, slicerPtr, maskCols[i]);
           break;
        case TpUInt:
          updateValue<uInt,Int64> (row, rowid, isSca, node, mask.array(), key.maskFirst(), col, slicerPtr, maskCols[i]);
           break;
        case TpInt64:
          updateValue<Int64,Int64> (row, rowid, isSca, node, mask.array(), key.maskFirst(), col, slicerPtr, maskCols[i]);
           break;
        case TpFloat:
          updateValue<Float,Int64> (row, rowid, isSca, node, mask.array(), key.maskFirst(), col, slicerPtr, maskCols[i]);
           break;
        case TpDouble:
          updateValue<Double,Int64> (row, rowid, isSca, node, mask.array(), key.maskFirst(), col, slicerPtr, maskCols[i]);
           break;
        case TpComplex:
          updateValue<Complex,Int64> (row, rowid, isSca, node, mask.array(), key.maskFirst(), col, slicerPtr, maskCols[i]);
           break;
        case TpDComplex:
          updateValue<DComplex,Int64> (row, rowid, isSca, node, mask.array(), key.maskFirst(), col, slicerPtr, maskCols[i]);
           break;
        default:
          throw TableInvExpr ("Column " + update_p[i]->columnName() +
                              " has an invalid data type for an"
                              " UPDATE with an integer value");
        }
        break;

      case TableExprNodeRep::NTDouble:
      case TableExprNodeRep::NTDate:
        switch (dtypeCol[i]) {
        case TpUChar:
          updateValue<uChar,Double> (row, rowid, isSca, node, mask.array(), key.maskFirst(), col, slicerPtr, maskCols[i]);
           break;
        case TpShort:
          updateValue<Short,Double> (row, rowid, isSca, node, mask.array(), key.maskFirst(), col, slicerPtr, maskCols[i]);
             break;
        case TpUShort:
          updateValue<uShort,Double> (row, rowid, isSca, node, mask.array(), key.maskFirst(), col, slicerPtr, maskCols[i]);
           break;
        case TpInt:
          updateValue<Int,Double> (row, rowid, isSca, node, mask.array(), key.maskFirst(), col, slicerPtr, maskCols[i]);
           break;
        case TpUInt:
          updateValue<uInt,Double> (row, rowid, isSca, node, mask.array(), key.maskFirst(), col, slicerPtr, maskCols[i]);
           break;
        case TpInt64:
          updateValue<Int64,Double> (row, rowid, isSca, node, mask.array(), key.maskFirst(), col, slicerPtr, maskCols[i]);
           break;
        case TpFloat:
          updateValue<Float,Double> (row, rowid, isSca, node, mask.array(), key.maskFirst(), col, slicerPtr, maskCols[i]);
           break;
        case TpDouble:
          updateValue<Double,Double> (row, rowid, isSca, node, mask.array(), key.maskFirst(), col, slicerPtr, maskCols[i]);
           break;
        case TpComplex:
          updateValue<Complex,Double> (row, rowid, isSca, node, mask.array(), key.maskFirst(), col, slicerPtr, maskCols[i]);
           break;
        case TpDComplex:
          updateValue<DComplex,Double> (row, rowid, isSca, node, mask.array(), key.maskFirst(), col, slicerPtr, maskCols[i]);
           break;
        default:
          throw TableInvExpr ("Column " + update_p[i]->columnName() +
                              " has an invalid data type for an"
                              " UPDATE with a double value");
        }
      break;

      case TableExprNodeRep::NTComplex:
        switch (dtypeCol[i]) {
        case TpComplex:
          updateValue<Complex,DComplex> (row, rowid, isSca, node, mask.array(), key.maskFirst(), col, slicerPtr, maskCols[i]);
           break;
        case TpDComplex:
          updateValue<DComplex,DComplex> (row, rowid, isSca, node, mask.array(), key.maskFirst(), col, slicerPtr, maskCols[i]);
           break;
        default:
          throw TableInvExpr ("Column " + update_p[i]->columnName() +
                              " has an invalid data type for an"
                              " UPDATE with a complex value");
        }
        break;
          
      default:
        throw TableInvExpr ("Unknown UPDATE expression data type");
      }
    }
  }
  if (showTimings) {
    timer.show ("  Update      ");
  }
}

template<typename TCOL, typename TNODE>
void TableParseSelect::copyMaskedValue (rownr_t row, ArrayColumn<TCOL>& acol,
                                        const Slicer* slicerPtr,
                                        const TNODE* val,
                                        size_t incr, const Array<Bool>& mask)
{
  // Get the array from the table.
  Array<TCOL> res(mask.shape());
  if (slicerPtr) {
    acol.getSlice (row, *slicerPtr, res);
  } else {
    acol.get (row, res);
  }
  // Copy values where masked.
  typename Array<TCOL>::iterator ito = res.begin();
  Array<Bool>::const_iterator imask = mask.begin();
  size_t n = res.size();
  for (size_t i=0; i<n; ++i) {
    if (*imask) {
      *ito = static_cast<TCOL>(*val);
    }
    ++ito;
    ++imask;
    val += incr;
  }
  // Put the array (slice).
  if (slicerPtr) {
    acol.putSlice (row, *slicerPtr, res);
  } else {
    acol.put (row, res);
  }
}

Array<Bool> TableParseSelect::makeMaskSlice (const Array<Bool>& mask,
                                             Bool maskFirst,
                                             const IPosition& shapeCol,
                                             const Slicer* slicerPtr)
{
  if (! slicerPtr  ||  maskFirst) {
    if (! mask.shape().isEqual (shapeCol)) {
      throw AipsError ("Update mask must conform the column's array shape");
    }
  }
  if (slicerPtr) {
    IPosition length;
    if (slicerPtr->isFixed()) {
      length = slicerPtr->length();
    } else {
      IPosition blc, trc, inc;
      length = slicerPtr->inferShapeFromSource (shapeCol, blc, trc, inc);
    }
    if (maskFirst) {
      // Mask before section, so apply the section to the mask.
      return mask(*slicerPtr);
    } else {
      if (! mask.shape().isEqual (length)) {
        throw AipsError ("Update mask must conform the column's array section");
      }
    }
  }
  return mask;
}

template<typename TCOL, typename TNODE>
void TableParseSelect::updateScalar (rownr_t row, const TableExprId& rowid,
                                     const TableExprNode& node,
                                     TableColumn& col)
{
  AlwaysAssert (node.isScalar(), AipsError);
  TNODE val;
  node.get (rowid, val);
  TCOL value(static_cast<TCOL>(val));
  col.putScalar (row, value);
}
template<typename TCOL, typename TNODE>
void TableParseSelect::updateArray (rownr_t row, const TableExprId& rowid,
                                    const TableExprNode& node,
                                    const Array<TNODE>& res,
                                    ArrayColumn<TCOL>& col)
{
  if (node.isScalar()  &&  col.isDefined (row)) {
    TNODE val;
    node.get (rowid, val);
    Array<TCOL> arr(col.shape(row));
    arr = static_cast<TCOL>(val);
    col.put (row, arr);
  } else {
    Array<TCOL> arr(res.shape());
    convertArray (arr, res);
    col.put (row, arr);
  }
}
template<typename TCOL, typename TNODE>
void TableParseSelect::updateSlice (rownr_t row, const TableExprId& rowid,
                                    const TableExprNode& node,
                                    const Array<TNODE>& res,
                                    const Slicer& slice,
                                    ArrayColumn<TCOL>& col)
{
  if (col.isDefined(row)) {
    if (node.isScalar()) {
      TNODE val;
      node.get (rowid, val);
      Array<TCOL> arr;
      if (slice.isFixed()) {
        arr.resize (slice.length());
      } else {
        // Unbound slicer, so derive from array shape.
        IPosition blc, trc, inc;
        arr.resize (slice.inferShapeFromSource
                    (col.shape(row), blc, trc, inc));
      }
      arr = static_cast<TCOL>(val);
      col.putSlice (row, slice, arr);
    } else {
      // Note that the calling function tests if the MArray is null.
      Array<TCOL> arr(res.shape());
      convertArray (arr, res);
      col.putSlice (row, slice, arr);
    }
  }
}

void TableParseSelect::checkMaskColumn (Bool hasMask,
                                        const ArrayColumn<Bool>& maskCol,
                                        const TableColumn& col)
{
  if (! maskCol.isNull()) {
    ///  if (maskCol.isNull()) {
    ///    if (hasMask) {
    ///      throw AipsError ("An update mask column must be given for a "
    ///                       "masked array expression in update of column " +
    ///                       col.columnDesc().name());
    ///    }
    ///  } else {
    if (! hasMask) {
      throw AipsError ("No update mask column can be given for an "
                       "unmasked expression in update of column " +
                       col.columnDesc().name());
    }
  }
}

template<typename TCOL, typename TNODE>
void TableParseSelect::updateValue (rownr_t row, const TableExprId& rowid,
                                    Bool isScalarCol,
                                    const TableExprNode& node,
                                    const Array<Bool>& mask,
                                    Bool maskFirst,
                                    TableColumn& col,
                                    const Slicer* slicerPtr,
                                    ArrayColumn<Bool>& maskCol)
{
  if (isScalarCol) {
    updateScalar<TCOL,TNODE> (row, rowid, node, col);
  } else {
    MArray<TNODE> aval;
    if (! node.isScalar()) {
      node.get (rowid, aval);
      if (aval.isNull()) {
        return;
      }
    }
    checkMaskColumn (aval.hasMask(), maskCol, col);
    ArrayColumn<TCOL> acol(col);
    if (mask.empty()) {
      if (slicerPtr) {
        updateSlice<TCOL,TNODE> (row, rowid, node, aval.array(),
                                 *slicerPtr, acol);
        if (! maskCol.isNull()) {
          updateSlice<Bool,Bool> (row, rowid, node, aval.mask(),
                                  *slicerPtr, maskCol);
        }
      } else {
        updateArray<TCOL,TNODE> (row, rowid, node, aval.array(), acol);
        if (! maskCol.isNull()) {
          updateArray<Bool,Bool> (row, rowid, node, aval.mask(), maskCol);
        }
      }
    } else {
      // A mask is used; can only be done if the column cell
      // contains an array.
      if (acol.isDefined(row)) {
        IPosition shapeCol = acol.shape (row);
        // Check shapes, get possible slice from mask.
        Array<Bool> smask(makeMaskSlice (mask, maskFirst, shapeCol,
                                         slicerPtr));
        // Get the expression data (scalar or array).
        TNODE sval;
        const TNODE* ptr = &sval;
        size_t incr = 0;
        Bool deleteIt;
        if (node.isScalar()) {
          node.get (rowid, sval);
        } else {
          if (! aval.shape().isEqual (smask.shape())) {
            throw TableInvExpr ("Array shapes in update of column " +
                                col.columnDesc().name() + " mismatch");
          }
          ptr = aval.array().getStorage (deleteIt);
          incr = 1;
        }
        // Put the array into the column (slice).
        // Copy values where masked.
        copyMaskedValue (row, acol, slicerPtr, ptr, incr, smask);
        if (! node.isScalar()) {
          aval.array().freeStorage (ptr, deleteIt);
          if (! maskCol.isNull()) {
            const Bool* bptr = aval.mask().getStorage (deleteIt);
            copyMaskedValue (row, maskCol, slicerPtr, bptr, 1, smask);
            aval.mask().freeStorage (bptr, deleteIt);
          }
        }
      }
    }
  }
}

//# Execute the inserts.
Table TableParseSelect::doInsert (Bool showTimings, Table& table)
{
  Timer timer;
  // Reopen the table for write.
  table.reopenRW();
  if (! table.isWritable()) {
    throw TableInvExpr ("Table " + table.tableName() + " is not writable");
  }
  // Add rows if the inserts are given as expressions.
  // Select rows and use update to put the expressions into the rows.
  if (update_p.size() > 0) {
    uInt  nexpr  = insertExprs_p.size();
    Int64 nrowex = nexpr / update_p.size();
    AlwaysAssert (nrowex*update_p.size() == nexpr, AipsError);
    Int64 nrow   = nrowex;
    if (limit_p > 0) {
      // See if #rows is given explicitly.
      nrow = limit_p;
    } else if (limit_p < 0) {
      nrow = table.nrow() + limit_p;
    }
    Vector<rownr_t> newRownrs(nrow);
    indgen (newRownrs, table.nrow());
    Vector<rownr_t> selRownrs(1, table.nrow() + nrow);
    // Add new rows to TableExprNodeRowid.
    // It works because NodeRowid does not obey disableApplySelection.
    for (std::vector<TableExprNode>::iterator iter=applySelNodes_p.begin();
         iter!=applySelNodes_p.end(); ++iter) {
      iter->disableApplySelection();
      iter->applySelection (selRownrs);
    }
    // Add one row at a time, because an insert expression might use
    // the table itself.
    Int64 inx = 0;
    for (Int64 i=0; i<nrow; ++i) {
      selRownrs[0] = table.nrow();
      table.addRow();
      Table sel = table(selRownrs);
      for (uInt j=0; j<update_p.size(); ++j) {
        update_p[j]->node() = insertExprs_p[inx*update_p.size() + j];
      }
      doUpdate (False, Table(), sel, selRownrs);
      inx++;
      if (inx == nrowex) inx = 0;
    }
    return table(newRownrs);
  }
  // Handle the inserts from another selection.
  // Do the selection.
  insSel_p->execute (False, False, False, 0);
  Table sel = insSel_p->getTable();
  if (sel.nrow() == 0) {
    return Table();
  }
  // Get the target columns if not given.
  if (columnNames_p.size() == 0) {
    columnNames_p = getStoredColumns (table);
  }
  // Get the source columns.
  Block<String> sourceNames;
  sourceNames = insSel_p->getColumnNames();
  if (sourceNames.size() == 0) {
    sourceNames = getStoredColumns (sel);
  }
  // Check if the number of columns match.
  if (sourceNames.size() != columnNames_p.size()) {
    throw TableInvExpr ("Error in INSERT command; nr of columns (=" +
                        String::toString(columnNames_p.size()) +
                        ") mismatches "
                        "number of columns in selection (=" +
                        String::toString(sourceNames.size()) + ")");
  }
  // Check if the data types match.
  const TableDesc& tdesc1 = table.tableDesc();
  const TableDesc& tdesc2 = sel.tableDesc();
  for (uInt i=0; i<columnNames_p.size(); i++) {
    if (tdesc1[columnNames_p[i]].trueDataType() !=
        tdesc2[sourceNames[i]].trueDataType()) {
      throw TableInvExpr ("Error in INSERT command; data type of columns " +
                          columnNames_p[i] + " and " + sourceNames[i] +
                          " mismatch");
    }
  }
  // Add the required nr of rows to the table and make a selection of it.
  rownr_t rownr = table.nrow();
  table.addRow (sel.nrow());
  Vector<rownr_t> rownrs(sel.nrow());
  indgen (rownrs, rownr);     // fill with rownr, rownr+1, etc.
  Table tab = table(rownrs);
  TableRow rowto (tab, Vector<String>(columnNames_p.begin(), columnNames_p.end()));
  ROTableRow rowfrom (sel, Vector<String>(sourceNames.begin(), sourceNames.end()));
  for (rownr_t i=0; i<sel.nrow(); i++) {
    rowto.put (i, rowfrom.get(i), False);
  }
  if (showTimings) {
    timer.show ("  Insert      ");
  }
  return tab;
}


//# Execute the deletes.
void TableParseSelect::doDelete (Bool showTimings, Table& table)
{
  //# If the selection is empty, return immediately.
  if (rownrs_p.empty()) {
    return;
  }
  Timer timer;
  // Reopen the table for write.
  table.reopenRW();
  if (! table.isWritable()) {
    throw TableInvExpr ("Table " + table.tableName() + " is not writable");
  }
  // Delete all rows.
  table.removeRow (rownrs_p);
  if (showTimings) {
    timer.show ("  Delete      ");
  }
}


//# Execute the counts.
Table TableParseSelect::doCount (Bool showTimings, const Table& table)
{
  Timer timer;
  // First do the column projection.
  Table intab = doProject (False, table);
  // Create an empty memory table with the same description as the input table.
  Table tab = TableCopy::makeEmptyMemoryTable ("", intab, True);
  // Add the Int64 _COUNT_ column.
  ScalarColumnDesc<Int64> countDesc ("_COUNT_");
  tab.addColumn (countDesc);
  ScalarColumn<Int64> countCol(tab, "_COUNT_");
  // Iterate for all columns through the input table.
  Vector<String> colNames = intab.tableDesc().columnNames();
  Block<String> bcolNames(colNames.size());
  std::copy (colNames.begin(), colNames.end(), bcolNames.begin());
  TableIterator iter (intab, bcolNames);
  while (!iter.pastEnd()) {
    Table tabfrom = iter.table();
    // Add one row containing the column values.
    rownr_t rownr = tab.nrow();
    tab.addRow();
    Table tabto = tab.project (bcolNames);
    TableCopy::copyRows (tabto, tabfrom, rownr, 0, 1);
    // Put the count.
    countCol.put (rownr, tabfrom.nrow());
    iter++;
  }
  if (showTimings) {
    timer.show ("  Count       ");
  }
  return tab;
}


//# Execute the groupby.
CountedPtr<TableExprGroupResult> TableParseSelect::doGroupby
(Bool showTimings, const std::vector<TableExprNodeRep*> aggrNodes, Int groupAggrUsed)
{
  Timer timer;
  // If only 'select count(*)' was given, get the size of the WHERE,
  // thus the size of rownrs_p.
  CountedPtr<TableExprGroupResult> result;
  if ((groupAggrUsed & ONLY_COUNTALL) != 0  &&
      (groupAggrUsed & GROUPBY) == 0) {
    result = doOnlyCountAll (aggrNodes[0]);
  } else {
    result = doGroupByAggr (aggrNodes);
  }
  if (showTimings) {
    timer.show ("  Groupby     ");
  }
  return result;
}

Table TableParseSelect::adjustApplySelNodes (const Table& table)
{
  for (std::vector<TableExprNode>::iterator iter=applySelNodes_p.begin();
       iter!=applySelNodes_p.end(); ++iter) {
    iter->applySelection (rownrs_p);
  }
  // Create the subset.
  Table tab(table(rownrs_p));
  // From now on use row numbers 0..n.
  indgen (rownrs_p);
  return tab;
}

void TableParseSelect::doHaving (Bool showTimings,
                                 const CountedPtr<TableExprGroupResult>& groups)
{
  Timer timer;
  // Find the rows matching the HAVING expression.
  Vector<rownr_t> rownrs(rownrs_p.size());
  rownr_t nr = 0;
  TableExprIdAggr rowid(groups);
  for (rownr_t i=0; i<rownrs_p.size(); ++i) {
    rowid.setRownr (rownrs_p[i]);
    if (havingNode_p.getBool (rowid)) {
      rownrs[nr++] = rownrs_p[i];
    }
  }
  // Use the found rows from now on.
  rownrs.resize (nr, True);
  rownrs_p.reference (rownrs);
  if (showTimings) {
    timer.show ("  Having      ");
  }
}

CountedPtr<TableExprGroupResult> TableParseSelect::doOnlyCountAll
(TableExprNodeRep* aggrNode)
{
  // This function is a special case because it does not need to
  // step though the table. Only its size is of interest. Furthermore,
  // some other columns can also be listed which will be those of the
  // last row.
  // Make a set containing the count(*) aggregate function object.
  std::vector<CountedPtr<TableExprGroupFuncSet> > funcSets
    (1, new TableExprGroupFuncSet());
  CountedPtr<TableExprGroupFuncBase> funcb = aggrNode->makeGroupAggrFunc();
  TableExprGroupCountAll& func = dynamic_cast<TableExprGroupCountAll&>(*funcb);
  // Note: add turns it into a CountedPtr, so it will be deleted automatically.
  funcSets[0]->add (funcb);
  // The nr of rows is the result of count(*), so simply set it.
  func.setResult (rownrs_p.size());
  // The resulting table has only 1 group; use the last row with it.
  if (! rownrs_p.empty()) {
    rownrs_p.reference (Vector<rownr_t>(1, rownrs_p[rownrs_p.size()-1]));
  }
  // Save the aggregation results in a result object.
  return CountedPtr<TableExprGroupResult>(new TableExprGroupResult(funcSets));
}

std::vector<CountedPtr<TableExprGroupFuncSet> >
TableParseSelect::doGroupByAggrMultipleKeys
(const std::vector<TableExprNodeRep*>& aggrNodes)
{
  // We have to group the data according to the (maybe empty) groupby.
  // We step through the table in the normal order which may not be the
  // groupby order.
  // A map<key,int> is used to keep track of the results where the int
  // is the index in a vector of a set of aggregate function objects.
  std::vector<CountedPtr<TableExprGroupFuncSet> > funcSets;
  std::map<TableExprGroupKeySet, int> keyFuncMap;
  // Create the set of groupby key objects.
  TableExprGroupKeySet keySet(groupbyNodes_p);
  // Loop through all rows.
  // For each row generate the key to get the right entry.
  TableExprId rowid(0);
  for (rownr_t i=0; i<rownrs_p.size(); ++i) {
    rowid.setRownr (rownrs_p[i]);
    keySet.fill (groupbyNodes_p, rowid);
    int groupnr = funcSets.size();
    std::map<TableExprGroupKeySet, int>::iterator iter=keyFuncMap.find (keySet);
    if (iter == keyFuncMap.end()) {
      keyFuncMap[keySet] = groupnr;
      funcSets.push_back (new TableExprGroupFuncSet (aggrNodes));
    } else {
      groupnr = iter->second;
    }
    funcSets[groupnr]->apply (rowid);
  }
  return funcSets;
}

CountedPtr<TableExprGroupResult> TableParseSelect::doGroupByAggr
(const std::vector<TableExprNodeRep*>& aggrNodes)
{
  // Get the aggregate functions to be evaluated lazily.
  std::vector<TableExprNodeRep*> immediateNodes;
  std::vector<TableExprNodeRep*> lazyNodes;
  for (uInt i=0; i<aggrNodes.size(); ++i) {
    aggrNodes[i]->makeGroupAggrFunc();
    if (aggrNodes[i]->isLazyAggregate()) {
      lazyNodes.push_back (aggrNodes[i]);
    } else {
      immediateNodes.push_back (aggrNodes[i]);
    }
  }
  uInt nimmediate = immediateNodes.size();
  // For lazy nodes a vector of TableExprId-s needs to be filled per group.
  // So add a node collecting the ids.
  // Note that this node must be alive after the if, so define outside if.
  TableExprAggrNode expridNode(TableExprFuncNode::gexpridFUNC,
                               TableExprNodeRep::NTInt,
                               TableExprNodeRep::VTArray,
                               TableExprNodeSet(),
                               std::vector<TENShPtr>(),
                               Block<Int>());
  if (! lazyNodes.empty()) {
    immediateNodes.push_back (&expridNode);
  }
  std::vector<CountedPtr<TableExprGroupFuncSet> > funcSets;
  // Use a faster way for a single groupby key.
  if (groupbyNodes_p.size() == 1  &&
      groupbyNodes_p[0].dataType() == TpDouble) {
    funcSets = doGroupByAggrSingleKey<Double> (immediateNodes);
  } else if (groupbyNodes_p.size() == 1  &&
             groupbyNodes_p[0].dataType() == TpInt) {
    funcSets = doGroupByAggrSingleKey<Int64> (immediateNodes);
  } else {
    funcSets = doGroupByAggrMultipleKeys (immediateNodes);
  }
  // Let the function nodes finish their operation.
  // Form the rownr vector from the rows kept in the aggregate objects.
  // Similarly, form the TableExprId vector if there are lazy nodes.
  Vector<rownr_t> rownrs(funcSets.size());
  std::vector<CountedPtr<std::vector<TableExprId> > > ids;
  ids.reserve (funcSets.size());
  rownr_t n=0;
  for (uInt i=0; i<funcSets.size(); ++i) {
    const std::vector<CountedPtr<TableExprGroupFuncBase> >& funcs
      = funcSets[i]->getFuncs();
    for (uInt j=0; j<funcs.size(); ++j) {
      funcs[j]->finish();
    }
    rownrs[n++] = funcSets[i]->getId().rownr();
    if (! lazyNodes.empty()) {
      ids.push_back (funcSets[i]->getFuncs()[nimmediate]->getIds());
    }
  }
  rownrs_p.reference (rownrs);
  // Save the aggregation results in a result object.
  CountedPtr<TableExprGroupResult> result
    (new TableExprGroupResult (funcSets, ids));
  return result;
}

void replaceIds (std::vector<CountedPtr<std::vector<TableExprId> > >& ids)
{
  // Combine all rowids in a single vector, so it can be sorted.
  Int64 nrow = 0;
  for (size_t i=0; i<ids.size(); ++i) {
    nrow += ids[i]->size();
  }
  Vector<Int64> rowids(nrow);
  Int64 inx = 0;
  for (size_t i=0; i<ids.size(); ++i) {
    std::vector<TableExprId>& vec = *ids[i];
    for (size_t j=0; j<vec.size(); ++j) {
      rowids[inx++] = vec[j].rownr();
    }
  }
  Vector<rownr_t> inxVec;
  GenSortIndirect<Int64,rownr_t>::sort (inxVec, rowids);
  // We need to replace each rowid by its sequence nr because a table selection
  // will map the selected rows to rowid 0..n.
  // So store the index in the rowids.
  for (rownr_t i=0; i<rowids.size(); ++i) {
    rowids[inxVec[i]] = i;
  }
  // Now replace the TableExprIds by the new rowids.
  inx = 0;
  for (size_t i=0; i<ids.size(); ++i) {
    std::vector<TableExprId>& vec = *ids[i];
    for (size_t j=0; j<vec.size(); ++j) {
      vec[j].setRownr (rowids[inx++]);
    }
  }
}

//# Execute the sort.
void TableParseSelect::doSort (Bool showTimings)
{
  //# If no rows, return immediately.
  //# (the code below will fail if empty)
  if (rownrs_p.empty()) {
    return;
  }
  Timer timer;
  uInt nrkey = sort_p.size();
  //# First check if the sort keys are correct.
  for (uInt i=0; i<nrkey; i++) {
    const TableParseSort& key = sort_p[i];
    //# This throws an exception for unknown data types (datetime, regex).
    key.node().getColumnDataType();
  }
  Block<void*> arrays(nrkey);
  Sort sort;
  Bool deleteIt;
  for (uInt i=0; i<nrkey; i++) {
    const TableParseSort& key = sort_p[i];
    switch (key.node().getColumnDataType()) {
    case TpBool:
      {
        Array<Bool>* array = new Array<Bool>
          (key.node().getColumnBool(rownrs_p));
        arrays[i] = array;
        const Bool* data = array->getStorage (deleteIt);
        sort.sortKey (data, TpBool, 0, getOrder(key));
        array->freeStorage (data, deleteIt);
      }
      break;
    case TpUChar:
      {
        Array<uChar>* array = new Array<uChar>
          (key.node().getColumnuChar(rownrs_p));
        arrays[i] = array;
        const uChar* data = array->getStorage (deleteIt);
        sort.sortKey (data, TpUChar, 0, getOrder(key));
        array->freeStorage (data, deleteIt);
      }
      break;
    case TpShort:
      {
        Array<Short>* array = new Array<Short>
          (key.node().getColumnShort(rownrs_p));
        arrays[i] = array;
        const Short* data = array->getStorage (deleteIt);
        sort.sortKey (data, TpShort, 0, getOrder(key));
        array->freeStorage (data, deleteIt);
      }
      break;
    case TpUShort:
      {
        Array<uShort>* array = new Array<uShort>
          (key.node().getColumnuShort(rownrs_p));
        arrays[i] = array;
        const uShort* data = array->getStorage (deleteIt);
        sort.sortKey (data, TpUShort, 0, getOrder(key));
        array->freeStorage (data, deleteIt);
      }
      break;
    case TpInt:
      {
        Array<Int>* array = new Array<Int>
          (key.node().getColumnInt(rownrs_p));
        arrays[i] = array;
        const Int* data = array->getStorage (deleteIt);
        sort.sortKey (data, TpInt, 0, getOrder(key));
        array->freeStorage (data, deleteIt);
      }
      break;
    case TpUInt:
      {
        Array<uInt>* array = new Array<uInt>
          (key.node().getColumnuInt(rownrs_p));
        arrays[i] = array;
        const uInt* data = array->getStorage (deleteIt);
        sort.sortKey (data, TpUInt, 0, getOrder(key));
        array->freeStorage (data, deleteIt);
      }
      break;
    case TpInt64:
      {
        Array<Int64>* array = new Array<Int64>
          (key.node().getColumnInt64(rownrs_p));
        arrays[i] = array;
        const Int64* data = array->getStorage (deleteIt);
        sort.sortKey (data, TpInt64, 0, getOrder(key));
        array->freeStorage (data, deleteIt);
      }
      break;
    case TpFloat:
      {
        Array<Float>* array = new Array<Float>
          (key.node().getColumnFloat(rownrs_p));
        arrays[i] = array;
        const Float* data = array->getStorage (deleteIt);
        sort.sortKey (data, TpFloat, 0, getOrder(key));
        array->freeStorage (data, deleteIt);
      }
      break;
    case TpDouble:
      {
        Array<Double>* array = new Array<Double>
          (key.node().getColumnDouble(rownrs_p));
        arrays[i] = array;
        const Double* data = array->getStorage (deleteIt);
        sort.sortKey (data, TpDouble, 0, getOrder(key));
        array->freeStorage (data, deleteIt);
      }
      break;
    case TpComplex:
      {
        Array<Complex>* array = new Array<Complex>
          (key.node().getColumnComplex(rownrs_p));
        arrays[i] = array;
        const Complex* data = array->getStorage (deleteIt);
        sort.sortKey (data, TpComplex, 0, getOrder(key));
        array->freeStorage (data, deleteIt);
      }
      break;
    case TpDComplex:
      {
        Array<DComplex>* array = new Array<DComplex>
          (key.node().getColumnDComplex(rownrs_p));
        arrays[i] = array;
        const DComplex* data = array->getStorage (deleteIt);
        sort.sortKey (data, TpDComplex, 0, getOrder(key));
        array->freeStorage (data, deleteIt);
      }
      break;
    case TpString:
      {
        Array<String>* array = new Array<String>
          (key.node().getColumnString(rownrs_p));
        arrays[i] = array;
        const String* data = array->getStorage (deleteIt);
        sort.sortKey (data, TpString, 0, getOrder(key));
        array->freeStorage (data, deleteIt);
      }
      break;
    default:
      AlwaysAssert (False, AipsError);
    }
  }
  rownr_t nrrow = rownrs_p.size();
  Vector<rownr_t> newRownrs (nrrow);
  int sortOpt = Sort::HeapSort;
  if (noDupl_p) {
    sortOpt += Sort::NoDuplicates;
  }
  sort.sort (newRownrs, nrrow, sortOpt);
  for (uInt i=0; i<nrkey; i++) {
    const TableParseSort& key = sort_p[i];
    switch (key.node().getColumnDataType()) {
    case TpBool:
      delete (Array<Bool>*)arrays[i];
      break;
    case TpUChar:
      delete (Array<uChar>*)arrays[i];
      break;
    case TpShort:
      delete (Array<Short>*)arrays[i];
      break;
    case TpUShort:
      delete (Array<uShort>*)arrays[i];
      break;
    case TpInt:
      delete (Array<Int>*)arrays[i];
      break;
    case TpUInt:
      delete (Array<uInt>*)arrays[i];
      break;
    case TpInt64:
      delete (Array<Int64>*)arrays[i];
      break;
    case TpFloat:
      delete (Array<Float>*)arrays[i];
      break;
    case TpDouble:
      delete (Array<Double>*)arrays[i];
      break;
    case TpComplex:
      delete (Array<Complex>*)arrays[i];
      break;
    case TpDComplex:
      delete (Array<DComplex>*)arrays[i];
      break;
    case TpString:
      delete (Array<String>*)arrays[i];
      break;
    default:
      AlwaysAssert (False, AipsError);
    }
  }
  if (showTimings) {
    timer.show ("  Orderby     ");
  }
  // Convert index to rownr.
  for (rownr_t i=0; i<newRownrs.size(); ++i) {
    newRownrs[i] = rownrs_p[newRownrs[i]];
  }
  rownrs_p.reference (newRownrs);
}


void TableParseSelect::doLimOff (Bool showTimings)
{
  Timer timer;
  Vector<rownr_t> newRownrs;
  // Negative values mean from the end (a la Python indexing).
  Int64 nrow = rownrs_p.size();
  if (offset_p < 0) {
    offset_p += nrow;
    if (offset_p < 0) offset_p = 0;
  }
  // A limit (i.e. nr of rows) or an endrow can be given (not both).
  // Convert a limit to endrow.
  if (limit_p != 0) {
    if (limit_p  < 0) limit_p  += nrow;
    endrow_p = offset_p + limit_p*stride_p;
  } else if (endrow_p != 0) {
    if (endrow_p < 0) endrow_p += nrow;
  } else {
    endrow_p = nrow;
  }
  if (endrow_p > nrow) endrow_p = nrow;
  if (offset_p < endrow_p) {
    Int64 nr = 1 + (endrow_p - offset_p - 1) / stride_p;
    newRownrs.reference (rownrs_p(Slice(offset_p, nr, stride_p)).copy());
  }
  rownrs_p.reference (newRownrs);
  if (showTimings) {
    timer.show ("  Limit/Offset");
  }
}

Table TableParseSelect::doLimOff (Bool showTimings, const Table& table)
{
  Timer timer;
  rownrs_p.resize (table.nrow());
  indgen (rownrs_p);
  doLimOff (False);
  return table(rownrs_p);
  if (showTimings) {
    timer.show ("  Limit/Offset");
  }
}


Table TableParseSelect::doProject
(Bool showTimings, const Table& table,
 const CountedPtr<TableExprGroupResult>& groups)
{
  Timer timer;
  Table tabp;
  // doProjectExpr might have been done for some columns, so clear first to avoid
  // they are calculated twice.
  update_p.clear();
  if (nrSelExprUsed_p > 0) {
    // Expressions used, so make a real table.
    tabp = doProjectExpr (False, groups);
  } else {
    // Only column names used, so make a reference table.
    tabp = table(rownrs_p);
    tabp = tabp.project (columnOldNames_p);
    for (uInt i=0; i<columnNames_p.size(); i++) {
      // Rename column if new name is given to a column.
      if (columnNames_p[i] != columnOldNames_p[i]) {
        tabp.renameColumn (columnNames_p[i], columnOldNames_p[i]);
      }
    }
  }
  if (showTimings) {
    timer.show ("  Projection  ");
  }
  if (distinct_p) {
    tabp = doDistinct (showTimings, tabp);
  }
  return tabp;
}

Table TableParseSelect::doProjectExpr
(Bool useSel, const CountedPtr<TableExprGroupResult>& groups)
{
  if (! rownrs_p.empty()) {
    // Add the rows if not done yet.
    if (projectExprTable_p.nrow() == 0) {
      projectExprTable_p.addRow (rownrs_p.size());
    }
    // Turn the expressions of the selected columns into update objects.
    for (uInt i=0; i<columnExpr_p.size(); i++) {
      if (! columnExpr_p[i].isNull()) {
        if (projectExprSelColumn_p[i] == useSel) {
          addUpdate (new TableParseUpdate (columnNames_p[i],
                                           columnNameMasks_p[i],
                                           columnExpr_p[i], False));
        }
      }
    }
    // Fill the columns in the table.
    doUpdate (False, Table(), projectExprTable_p, rownrs_p, groups);
    projectExprTable_p.flush();
  }
  return projectExprTable_p;
}

Table TableParseSelect::doFinish (Bool showTimings, Table& table,
                                  const std::vector<const Table*>& tempTables,
                                  const std::vector<TableParseSelect*>& stack)
{
  Timer timer;
  Table result(table);
  // If the table name contains ::, a subtable has to be created.
  // Split the name at the last ::.
  Vector<String> parts = stringToVector(resultName_p, std::regex("::"));
  Table parent;
  String fullName (resultName_p);
  if (parts.size() > 1) {
    parent = openParentTable (resultName_p, parts[parts.size()-1],
                              tempTables, stack);
    fullName = parent.tableName() + '/' + parts[parts.size()-1];
  }
  if (resultType_p == 1) {
    if (table.tableType() != Table::Memory) {
      result = table.copyToMemoryTable (fullName);
    }
  } else if (! resultCreated_p) {
    if (resultType_p > 0) {
      table.deepCopy (fullName, dminfo_p, storageOption_p,
                      overwrite_p ? Table::New : Table::NewNoReplace,
                      True, endianFormat_p);
      result = Table(fullName);
    } else {
      // Normal reference table.
      table.rename (fullName,
                    overwrite_p ? Table::New : Table::NewNoReplace);
      table.flush();
    }
  }
  // Create a subtable keyword if needed.
  if (parts.size() > 1) {
    parent.reopenRW();
    parent.rwKeywordSet().defineTable (parts[parts.size()-1], table);
  }
  if (showTimings) {
    timer.show ("  Giving      ");
  }
  return result;
}

DataType TableParseSelect::makeDataType (DataType dtype, const String& dtstr,
                                         const String& colName)
{
  if (! dtstr.empty()) {
    if (dtstr == "B") {
      if (dtype != TpOther  &&  dtype != TpBool) {
        throw TableInvExpr ("Expression of column " + colName +
                            " does not have data type Bool");
      }
      return TpBool;
    }
    if (dtstr == "S") {
      if (dtype != TpOther  &&  dtype != TpString) {
        throw TableInvExpr ("Expression of column " + colName +
                            " does not have data type String");
      }
      return TpString;
    }
    if (dtype == TpBool  ||  dtype == TpString) {
      throw TableInvExpr ("Expression of column " + colName +
                          " does not have a numeric data type");
    }
    // Any numeric data type can be converted to Complex.
    if (dtstr == "C4") {
      return TpComplex;
    } else if (dtstr == "C8") {
      return TpDComplex;
    }
    // Real numeric data types cannot have a complex value.
    if (dtype == TpComplex  ||  dtype == TpDComplex) {
      throw TableInvExpr ("Expression of column " + colName +
                          " does not have a real numeric data type");
    }
    if (dtstr == "U1") {
      return TpUChar;
    } else if (dtstr == "I2") {
      return TpShort;
    } else if (dtstr == "U2") {
      return TpUShort;
    } else if (dtstr == "I4") {
      return TpInt;
    } else if (dtstr == "U4") {
      return TpUInt;
    } else if (dtstr == "I8") {
      return TpInt64;
    } else if (dtstr == "R4") {
      return TpFloat;
    } else if (dtstr == "R8") {
      return TpDouble;
    } else if (dtstr == "EPOCH") {
      return TpQuantity;
    }
    throw TableInvExpr ("Datatype " + dtstr + " of column " + colName +
                        " is invalid");
  }
  if (dtype == TpOther) {
    throw TableInvExpr ("Datatype " + dtstr + " of column " + colName +
                        " is invalid (maybe a set with incompatible units)");
  }
  return dtype;
}

void TableParseSelect::addColumnDesc (TableDesc& td,
                                      DataType dtype,
                                      const String& colName,
                                      Int options,
                                      Int ndim, const IPosition& shape,
                                      const String& dmType,
                                      const String& dmGroup,
                                      const String& comment,
                                      const TableRecord& keywordSet,
                                      const Vector<String>& unitName,
                                      const Record& attributes)
{
  if (ndim < 0) {
    switch (dtype) {
    case TpBool:
      td.addColumn (ScalarColumnDesc<Bool> (colName, comment,
                                            dmType, dmGroup, options));
      break;
    case TpUChar:
      td.addColumn (ScalarColumnDesc<uChar> (colName, comment,
                                             dmType, dmGroup, 0, options));
      break;
    case TpShort:
      td.addColumn (ScalarColumnDesc<Short> (colName, comment,
                                             dmType, dmGroup, 0, options));
      break;
    case TpUShort:
      td.addColumn (ScalarColumnDesc<uShort> (colName, comment,
                                              dmType, dmGroup, 0, options));
      break;
    case TpInt:
      td.addColumn (ScalarColumnDesc<Int> (colName, comment,
                                           dmType, dmGroup, 0, options));
      break;
    case TpUInt:
      td.addColumn (ScalarColumnDesc<uInt> (colName, comment,
                                            dmType, dmGroup, 0, options));
      break;
    case TpInt64:
      td.addColumn (ScalarColumnDesc<Int64> (colName, comment,
                                             dmType, dmGroup, 0, options));
      break;
    case TpFloat:
      td.addColumn (ScalarColumnDesc<Float> (colName, comment,
                                             dmType, dmGroup, options));
      break;
    case TpDouble:
    case TpQuantity:
      td.addColumn (ScalarColumnDesc<Double> (colName, comment,
                                              dmType, dmGroup, options));
      break;
    case TpComplex:
      td.addColumn (ScalarColumnDesc<Complex> (colName, comment,
                                               dmType, dmGroup, options));
      break;
    case TpDComplex:
      td.addColumn (ScalarColumnDesc<DComplex> (colName, comment,
                                                dmType, dmGroup, options));
      break;
    case TpString:
      td.addColumn (ScalarColumnDesc<String> (colName, comment,
                                              dmType, dmGroup, options));
      break;
    default:
      AlwaysAssert (False, AipsError);
    }
  } else {
    // Giving a shape means fixed shape arrays.
    if (shape.size() > 0) {
      options |= ColumnDesc::FixedShape;
    }
    switch (dtype) {
    case TpBool:
      td.addColumn (ArrayColumnDesc<Bool> (colName, comment,
                                           dmType, dmGroup,
                                           shape, options, ndim));
      break;
    case TpUChar:
      td.addColumn (ArrayColumnDesc<uChar> (colName, comment,
                                            dmType, dmGroup,
                                            shape, options, ndim));
      break;
    case TpShort:
      td.addColumn (ArrayColumnDesc<Short> (colName, comment,
                                            dmType, dmGroup,
                                            shape, options, ndim));
      break;
    case TpUShort:
      td.addColumn (ArrayColumnDesc<uShort> (colName, comment,
                                             dmType, dmGroup,
                                             shape, options, ndim));
      break;
    case TpInt:
      td.addColumn (ArrayColumnDesc<Int> (colName, comment,
                                          dmType, dmGroup,
                                          shape, options, ndim));
      break;
    case TpUInt:
      td.addColumn (ArrayColumnDesc<uInt> (colName, comment,
                                           dmType, dmGroup,
                                           shape, options, ndim));
      break;
    case TpInt64:
      td.addColumn (ArrayColumnDesc<Int64> (colName, comment,
                                            dmType, dmGroup,
                                            shape, options, ndim));
      break;
    case TpFloat:
      td.addColumn (ArrayColumnDesc<Float> (colName, comment,
                                            dmType, dmGroup,
                                            shape, options, ndim));
      break;
    case TpDouble:
    case TpQuantity:
      td.addColumn (ArrayColumnDesc<Double> (colName, comment,
                                             dmType, dmGroup,
                                             shape, options, ndim));
      break;
    case TpComplex:
      td.addColumn (ArrayColumnDesc<Complex> (colName, comment,
                                              dmType, dmGroup,
                                              shape, options, ndim));
      break;
    case TpDComplex:
      td.addColumn (ArrayColumnDesc<DComplex> (colName, comment,
                                               dmType, dmGroup,
                                               shape, options, ndim));
      break;
    case TpString:
      td.addColumn (ArrayColumnDesc<String> (colName, comment,
                                             dmType, dmGroup,
                                             shape, options, ndim));
      break;
    default:
      AlwaysAssert (False, AipsError);
    }
  }
  // Write the keywords.
  ColumnDesc& cd = td.rwColumnDesc(colName);
  TableRecord keys (keywordSet);
  keys.merge (TableRecord(attributes),
              RecordInterface::OverwriteDuplicates);
  // If no keys defined for this column, define Epoch measure for dates.
  if (dtype == TpQuantity  &&  keys.empty()) {
    TableRecord r;
    r.define ("type", "epoch");
    r.define ("Ref", "UTC");
    keys.defineRecord ("MEASINFO", r);
  }
  cd.rwKeywordSet() = keys;
  // Write unit in column keywords (in TableMeasures compatible way).
  // Check if it is valid by constructing the Unit object.
  Vector<String> unit(unitName);
  if (dtype == TpQuantity  &&  unit.empty()) {
    unit = Vector<String>(1, "d");
  }
  if (! unit.empty()  &&  ! unit[0].empty()) {
    if (! shape.empty()) {
      if (! (unit.size() == 1  ||  unit.size() == uInt(shape[0]))) {
        throw AipsError("Nr of units must be 1 or match the first axis");
      }
    }
    cd.rwKeywordSet().define ("QuantumUnits", unit);
  }
}

std::pair<ColumnDesc,Record> TableParseSelect::findColumnInfo
(const String& colName, const String& newColName) const
{
  String columnName, shorthand;
  Vector<String> fieldNames;
  if (splitName (shorthand, columnName, fieldNames, colName, True, False, True)) {
    throw TableInvExpr ("Column name " + colName + " is a keyword, no column");
  }
  Table tab = findTable (shorthand, True);
  if (tab.isNull()) {
    throw TableInvExpr("Shorthand " + shorthand + " has not been defined");
  }
  Record dminfo = tab.dataManagerInfo();
  // Try to find the column in the info.
  // If found, create a dminfo record for this column only.
  Record dmrec;
  for (uInt i=0; i<dminfo.nfields(); ++i) {
    Record dm(dminfo.subRecord(i));
    if (dm.isDefined("COLUMNS")) {
      Vector<String> cols(dm.asArrayString("COLUMNS"));
      if (std::find(cols.begin(), cols.end(), columnName) != cols.end()) {
        dm.define ("COLUMNS", Vector<String>(1, newColName));
        dmrec.defineRecord (0, dm);
        break;
      }
    }
  }
  return std::make_pair (tab.tableDesc().columnDesc(columnName), dmrec);
}

Table TableParseSelect::doDistinct (Bool showTimings, const Table& table)
{
  Timer timer;
  Table result;
  // Sort the table uniquely on all columns.
  Table tabs = table.sort (columnNames_p, Sort::Ascending,
                           Sort::QuickSort|Sort::NoDuplicates);
  if (tabs.nrow() == table.nrow()) {
    // Everything was already unique.
    result = table;
  } else {
    // Get the rownumbers.
    // Make sure it does not reference an internal array.
    Vector<rownr_t> rownrs(tabs.rowNumbers(table));
    rownrs.unique();
    // Put the rownumbers back in the original order.
    GenSort<rownr_t>::sort (rownrs);
    result = table(rownrs);
    rownrs_p.reference (rownrs);
  }
  if (showTimings) {
    timer.show ("  Distinct    ");
  }
  return result;
}


//# Keep the name of the resulting table.
void TableParseSelect::handleGiving (const String& name, const Record& rec)
{
  resultName_p = name;
  for (uInt i=0; i<rec.nfields(); ++i) {
    String fldName = rec.name(i);
    fldName.downcase();
    Bool done=False;
    if (rec.dataType(i) == TpBool) {
      done = True;
      if (fldName == "memory") {
        resultType_p = 1;
      } else if (fldName == "scratch") {
        resultType_p = 2;
      } else if (fldName == "plain") {
        resultType_p = 3;
      } else if (fldName == "plain_big") {
        resultType_p   = 3;
        endianFormat_p = Table::BigEndian;
      } else if (fldName == "plain_little") {
        resultType_p   = 3;
        endianFormat_p = Table::LittleEndian;
      } else if (fldName == "plain_local") {
        resultType_p   = 3;
        endianFormat_p = Table::LocalEndian;
      } else if (fldName == "overwrite") {
        overwrite_p = rec.asBool(i);
      } else {
        done = False;
      }
    }
    if (done) {
      if (fldName != "overwrite"  &&  !rec.asBool(i)) {
        throw TableParseError ("Field name " + rec.name(i) +
                               " should not have a False value");
      }
    } else if (fldName == "type") {
      Bool ok = False;
      if (rec.dataType(i) == TpString) {
        ok = True;
        String str = rec.asString(i);
        str.downcase();
        if (str == "plain") {
          resultType_p = 3;
        } else if (str == "scratch") {
          resultType_p = 2;
        } else if (str == "memory") {
          resultType_p = 1;
        } else {
          ok = False;
        }
      }
      if (!ok) {
        throw TableParseError("type must have a string value "
                              "plain, scratch or memory");
      }
    } else if (fldName == "endian") {
      Bool ok = False;
      if (rec.dataType(i) == TpString) {
        ok = True;
        String str = rec.asString(i);
        str.downcase();
        if (str == "big") {
          endianFormat_p = Table::BigEndian;
        } else if (str == "little") {
          endianFormat_p = Table::LittleEndian;
        } else if (str == "local") {
          endianFormat_p = Table::LocalEndian;
        } else if (str == "aipsrc") {
          endianFormat_p = Table::AipsrcEndian;
        } else {
          ok = False;
        }
      }
      if (!ok) {
        throw TableParseError("endian must have a string value "
                              "big, little, local or aipsrc");
      }
    } else if (fldName == "storage") {
      Bool ok = False;
      if (rec.dataType(i) == TpString) {
        ok = True;
        String str = rec.asString(i);
        str.downcase();
        if (str == "multifile") {
          storageOption_p.setOption (StorageOption::MultiFile);
        } else if (str == "multihdf5") {
          storageOption_p.setOption (StorageOption::MultiHDF5);
        } else if (str == "sepfile") {
          storageOption_p.setOption (StorageOption::SepFile);
        } else if (str == "default") {
          storageOption_p.setOption (StorageOption::Default);
        } else if (str == "aipsrc") {
          storageOption_p.setOption (StorageOption::Aipsrc);
        } else {
          ok = False;
        }
      }
      if (!ok) {
        throw TableParseError("storage must have a string value "
                              "multifile, multihdf5, sepfile, default or aipsrc");
      }
    } else if (fldName == "blocksize") {
      try {
        storageOption_p.setBlockSize (rec.asInt(i));
      } catch (...) {
        throw TableParseError("blocksize must have an integer value");
      }
    } else {
      throw TableParseError(rec.name(i) + " is an invalid table options field");
    }
  }
  if (resultName_p.empty()  &&  resultType_p > 2) {
    throw TableParseError ("output table name can only be omitted if "
                           "AS MEMORY or AS SCRATCH is given");
  }
}

//# Keep the resulting set expression.
void TableParseSelect::handleGiving (const TableExprNodeSet& set)
{
  // In principle GIVING is handled before SELECT, so below is always false.
  // But who knows what future brings us.
  if (! columnNames_p.empty()) {
    throw TableInvExpr("Expressions can be given in SELECT or GIVING, "
                       "not both");
  }
  resultSet_p = new TableExprNodeSet (set);
  resultSet_p->checkAggrFuncs();
}

void TableParseSelect::checkTableProjSizes() const
{
  // Check if all tables used in non-constant select expressions
  // have the same size as the first table.
  rownr_t nrow = fromTables_p[0].table().nrow();
  for (uInt i=0; i<columnExpr_p.size(); i++) {
    if (! columnExpr_p[i].getRep()->isConstant()) {
      if (columnExpr_p[i].getRep()->nrow() != nrow) {
        throw TableInvExpr("Nr of rows of tables used in select "
                           "expressions must be equal to first table");
      }
    }
  }
}

//# Execute all parts of a TaQL command doing some selection.
void TableParseSelect::execute (Bool showTimings, Bool setInGiving,
                                Bool mustSelect, rownr_t maxRow,
                                Bool doTracing,
                                const std::vector<const Table*>& tempTables,
                                const std::vector<TableParseSelect*>& stack)
{
  //# A selection query consists of:
  //#  - SELECT to do projection
  //#     can only refer to columns in FROM or can be constants
  //#     can contain aggregate functions
  //#  - FROM to tell the tables to use
  //#  - WHERE to select rows from tables
  //#     can only refer to columns in FROM
  //#     cannot contain aggregate functions
  //#  - GROUPBY to group result of WHERE
  //#     can refer to columns in FROM or expressions of FROM
  //#     (in SQL92 only to columns in FROM)
  //#     cannot contain aggregate functions
  //#  - HAVING to select groups
  //#     can refer to column in SELECT or FROM
  //#     HAVING is possible without GROUPBY (-> one group only)
  //#     usually refers to aggregate functions/columns
  //#     if non-aggregate function is used, glast is implied
  //#  - apply combination (UNION, INTERSECTION, DIFFERENCE)
  //#     must have equal SELECT result (with equal column names)
  //#  - ORDERBY to sort result of HAVING
  //#     can refer to columns in SELECT or FROM
  //#     (in SQL92 only to columns in SELECT), thus look in SELECT first
  //#     can contain aggregate functions if aggregation or GROUPBY is used
  //#  - LIMIT to skip latest results of ORDERBY
  //#  - OFFSET to ignore first results of ORDERBY
  //# If GROUPBY/aggr is used, all clauses can contain other columns than
  //# aggregate or GROUPBY columns. The last row in a group is used for them.

  //# Set limit if not given.
  if (limit_p == 0) {
    limit_p = maxRow;
    if (doTracing  &&  limit_p) {
      cerr << "LIMIT not given; set to " << limit_p << endl;
    }
  }
  //# Give an error if no command part has been given.
  if (mustSelect  &&  commandType_p == PSELECT
  &&  node_p.isNull()  &&  sort_p.size() == 0
  &&  columnNames_p.size() == 0  &&  resultSet_p == 0
  &&  limit_p == 0  &&  endrow_p == 0  &&  stride_p == 1  &&  offset_p == 0) {
    throw (TableInvExpr
           ("TableParse error: no projection, selection, sorting, "
            "limit, offset, or giving-set given in SELECT command"));
  }
  // Test if a "giving set" is possible.
  if (resultSet_p != 0  &&  !setInGiving) {
    throw TableInvExpr ("A query in a FROM can only have "
                        "'GIVING tablename'");
  }
  //# Set the project expressions to be filled in first stage.
  makeProjectExprSel();
  //# Get nodes representing aggregate functions.
  //# Test if aggregate, groupby, or having is used.
  std::vector<TableExprNodeRep*> aggrNodes;
  Int groupAggrUsed = testGroupAggr (aggrNodes);
  if (groupAggrUsed == 0) {
    // Check if tables used in projection have the same size.
    checkTableProjSizes();
  } else if (doTracing) {
    cerr << "GROUPBY to be done using " << aggrNodes.size()
         << " aggregate nodes" << endl;
  }
  // Column nodes used in aggregate functions should not adhere applySelection.
  uInt ndis = 0;
  for (uInt i=0; i<aggrNodes.size(); ++i) {
    std::vector<TableExprNodeRep*> colNodes;
    aggrNodes[i]->getColumnNodes (colNodes);
    for (uInt j=0; j<colNodes.size(); ++j) {
      colNodes[j]->disableApplySelection();
      ndis++;
    }
  }
  if (doTracing) {
    cerr << "  disableApplySelection done in " << ndis
         << " column nodes of aggregate nodes" << endl;
  }
  // Select distinct makes no sense if aggregate and no groupby is given.
  if (groupAggrUsed != 0  &&  (groupAggrUsed & GROUPBY) == 0) {
    distinct_p = False;
  }
  //# The first table in the list is the source table.
  Table table = fromTables_p[0].table();
  //# Set endrow_p if positive limit and positive or no offset.
  if (offset_p >= 0  &&  limit_p > 0) {
    endrow_p = offset_p + limit_p * stride_p;
  }
  //# Determine if we can pre-empt the selection loop.
  //# That is possible if a positive limit and offset are given
  //# without sorting, select distinct, groupby, or aggregation.
  rownr_t nrmax=0;
  if (endrow_p > 0  &&  sort_p.size() == 0  &&  !distinct_p  &&
      groupAggrUsed == 0) {
    nrmax = endrow_p;
    if (doTracing) {
      cerr << "pre-empt WHERE at " << nrmax << " rows" << endl;
    }
  }
  //# First do the where selection.
  Table resultTable(table);
  if (! node_p.isNull()) {
//#//        cout << "Showing TableExprRange values ..." << endl;
//#//        Block<TableExprRange> rang;
//#//        node_p->ranges(rang);
//#//        for (Int i=0; i<rang.size(); i++) {
//#//            cout << rang[i].getColumn().columnDesc().name() << rang[i].start()
//#//                 << rang[i].end() << endl;
//#//        }
    Timer timer;
    resultTable = table(node_p, nrmax);
    if (showTimings) {
      timer.show ("  Where       ");
    }
    if (doTracing) {
      cerr << "WHERE resulted in " << resultTable.nrow() << " rows" << endl;
    }
  }
  // Get the row numbers of the result of the possible first step.
  rownrs_p.reference (resultTable.rowNumbers(table));
  // Execute possible groupby/aggregate.
  CountedPtr<TableExprGroupResult> groupResult;
  if (groupAggrUsed != 0) {
    groupResult = doGroupby (showTimings, aggrNodes, groupAggrUsed);
    // Aggregate results and normal table rows need to have the same rownrs,
    // so set the selected rows in the table column objects.
    resultTable = adjustApplySelNodes(table);
    table = resultTable;
    if (doTracing) {
      cerr << "GROUPBY resulted in " << table.nrow() << " groups" << endl;
      cerr << "  applySelection called for " << applySelNodes_p.size()
           << " nodes" << endl;
    }
  }
  // Do the projection of SELECT columns used in HAVING or ORDERBY.
  // Thereafter the column nodes need to use rownrs 0..n.
  if (! projectExprSubset_p.empty()) {
    doProjectExpr (True, groupResult);
    resultTable = adjustApplySelNodes(table);
    table = resultTable;
    if (doTracing) {
      cerr << "Pre-projected " << projectExprSubset_p.size()
           << " columns" << endl;
      cerr << "  applySelection called for " << applySelNodes_p.size()
           << " nodes" << endl;
    }
  }
  // Do the possible HAVING step.
  if (! havingNode_p.isNull()) {
    doHaving (showTimings, groupResult);
    if (doTracing) {
      cerr << "HAVING resulted in " << rownrs_p.size() << " rows" << endl;
    }
  }
  //# Then do the sort.
  if (sort_p.size() > 0) {
    doSort (showTimings);
    if (doTracing) {
      cerr << "ORDERBY resulted in " << rownrs_p.size() << " rows" << endl;
    }
  }
  // If select distinct is given, limit/offset can only be done thereafter
  // because duplicate rows will be removed.
  if (!distinct_p  &&  (offset_p != 0  ||  limit_p != 0  ||
                        endrow_p != 0  || stride_p != 1)) {
    doLimOff (showTimings);
    if (doTracing) {
      cerr << "LIMIT/OFFSET resulted in " << rownrs_p.size() << " rows" << endl;
    }
  }
  // Take the correct rows of the projected table (if not empty).
  resultTable = table(rownrs_p);
  if (projectExprTable_p.nrow() > 0) {
    if (rownrs_p.size() < projectExprTable_p.nrow()  ||  sort_p.size() > 0) {
      projectExprTable_p = projectExprTable_p(rownrs_p);
      // Make deep copy if stored in a table.
      if (resultType_p == 3) {
        projectExprTable_p.rename (resultName_p + "_tmpproject",
                                   Table::New);
        projectExprTable_p.deepCopy
          (resultName_p, dminfo_p, storageOption_p,
           overwrite_p ? Table::New : Table::NewNoReplace,
           True, endianFormat_p);
        projectExprTable_p = Table(resultName_p);
        TableUtil::deleteTable (resultName_p + "_tmpproject");
        // Indicate it does not have to be created anymore.
        resultCreated_p = True;
      }
      resultTable = projectExprTable_p;
    }
  }
  //# Then do the update, delete, insert, or projection and so.
  if (commandType_p == PUPDATE) {
    doUpdate (showTimings, table, resultTable, rownrs_p);
    table.flush();
  } else if (commandType_p == PINSERT) {
    Table tabNewRows = doInsert (showTimings, table);
    table.flush();
    resultTable = tabNewRows;
  } else if (commandType_p == PDELETE) {
    doDelete (showTimings, table);
    table.flush();
  } else if (commandType_p == PCOUNT) {
    resultTable = doCount (showTimings, table);
  } else {
    //# Then do the projection.
    if (columnNames_p.size() > 0) {
      resultTable = doProject (showTimings, table, groupResult);
      if (doTracing) {
        cerr << "Final projection done of "
             << columnNames_p.size() - projectExprSubset_p.size()
             << " columns resulting in " << resultTable.nrow()
             << " rows" << endl;
      }
    }
    // If select distinct is given, limit/offset must be done at the end.
    if (distinct_p  &&  (offset_p != 0  ||  limit_p != 0  ||
                         endrow_p != 0  || stride_p != 1)) {
      resultTable = doLimOff (showTimings, resultTable);
      if (doTracing) {
        cerr << "LIMIT/OFFSET resulted in " << resultTable.nrow()
             << " rows" << endl;
      }
    }
    //# Finally rename or copy using the given name (and flush it).
    if (resultType_p != 0  ||  ! resultName_p.empty()) {
      resultTable = doFinish (showTimings, resultTable, tempTables, stack);
      if (doTracing) {
        cerr << "Finished the GIVING command" << endl;
      }
    }
  }
  //# Keep the table for later.
  table_p = resultTable;
}

void TableParseSelect::checkAggrFuncs (const TableExprNode& node)
{
  if (! node.isNull()) {
    node.getRep()->checkAggrFuncs();
  }
}
//# Get aggregate functions used and check if used at correct places.
//# Also check that HAVING is not solely used.
Int TableParseSelect::testGroupAggr (std::vector<TableExprNodeRep*>& aggr) const
{
  // Make sure main (where) node does not have aggregate functions.
  // This has been checked before, but use defensive programming.
  if (! node_p.isNull()) {
    node_p.getRep()->getAggrNodes (aggr);
    AlwaysAssert (aggr.empty(), AipsError);
  }
  // Get possible aggregate functions used in SELECT and HAVING.
  for (uInt i=0; i<columnExpr_p.size(); ++i) {
    const_cast<TableExprNodeRep*>(columnExpr_p[i].getRep().get())->getAggrNodes (aggr);
  }
  uInt nselAggr = aggr.size();
  if (! havingNode_p.isNull()) {
    const_cast<TableExprNodeRep*>(havingNode_p.getRep().get())->getAggrNodes (aggr);
  }
  // Make sure aggregate functions are not used in a UPDATE command, etc.
  // Again, this cannot happen but use defensive programming.
  if (commandType_p != PSELECT) {
    AlwaysAssert (aggr.empty(), AipsError);
    return 0;
  }
  // Make sure HAVING is only used if SELECT has an aggregate function
  // or if GROUPBY is used.
  if (! havingNode_p.isNull()) {
    if (nselAggr == 0  &&  groupbyNodes_p.empty()) {
      throw TableInvExpr ("HAVING can only be used if GROUPBY is used or "
                          "an aggregate function is used in SELECT");
    }
  }
  // Test if any group/aggr is given or if only
  // 'SELECT COUNT(*)' is given without GROUPBY.
  Int res = 0;
  if (! groupbyNodes_p.empty()) res += GROUPBY;
  if (! aggr.empty())           res += AGGR_FUNCS;
  if (nselAggr == 1  &&  aggr.size() == 1) {
    TableExprAggrNode* node = dynamic_cast<TableExprAggrNode*>(aggr[0]);
    if (node  &&  node->funcType() == TableExprFuncNode::countallFUNC) {
      res += ONLY_COUNTALL;
    }
  }
  return res;
}

String TableParseSelect::getTableInfo (const Vector<String>& parts,
                                       const TaQLStyle& style)
{
  Bool showdm = False;
  Bool showcol = True;
  Bool showsub = False;
  Bool sortcol = False;
  Bool tabkey = False;
  Bool colkey = False;
  for (uInt i=2; i<parts.size(); ++i) {
    String opt(parts[i]);
    opt.downcase();
    Bool fop = True;
    if (opt.size() > 2   &&  opt.substr(0,2) == "no") {
      fop = False;
      opt = opt.substr(2);
    }
    if (opt == "dm") {
      showdm = fop;
    } else if (opt == "col") {
      showcol = fop;
    } else if (opt == "sort") {
      sortcol = fop;
    } else if (opt == "key") {
      tabkey = fop;
      colkey = fop;
    } else if (opt == "tabkey") {
      tabkey = fop;
    } else if (opt == "colkey") {
      colkey = fop;
    } else if (opt == "recur") {
      showsub = fop;
    } else {
      throw AipsError (parts[i] + " is an unknown show table option; use: "
                       "dm col sort key colkey recur");
    }
  }
  std::ostringstream os;
  fromTables_p[0].table().showStructure (os, showdm, showcol, showsub,
                                         sortcol, style.isCOrder());
  fromTables_p[0].table().showKeywords (os, showsub, tabkey, colkey);
  return os.str();
}


void TableParseSelect::show (ostream& os) const
{
  if (! node_p.isNull()) {
    node_p.show (os);
  }
}


//# Simplified forms of general tableCommand function.
TaQLResult tableCommand (const String& str)
{
  Vector<String> cols;
  return tableCommand (str, cols);
}
TaQLResult tableCommand (const String& str, const Table& tempTable)
{
  std::vector<const Table*> tmp(1);
  tmp[0] = &tempTable;
  return tableCommand (str, tmp);
}
TaQLResult tableCommand (const String& str,
                         const std::vector<const Table*>& tempTables)
{
  Vector<String> cols;
  return tableCommand (str, tempTables, cols);
}
TaQLResult tableCommand (const String& str, Vector<String>& cols)
{
  std::vector<const Table*> tmp;
  return tableCommand (str, tmp, cols);
}

TaQLResult tableCommand (const String& str,
                         Vector<String>& cols,
                         String& commandType)
{
  std::vector<const Table*> tmp;
  return tableCommand (str, tmp, cols, commandType);
}

TaQLResult tableCommand (const String& str,
                         const std::vector<const Table*>& tempTables,
                         Vector<String>& cols)
{
  String commandType;
  return tableCommand (str, tempTables, cols, commandType);
}

//# Do the actual parsing of a command and execute it.
TaQLResult tableCommand (const String& str,
                         const std::vector<const Table*>& tempTables,
                         Vector<String>& cols,
                         String& commandType)
{
  commandType = "error";
  // Do the first parse step. It returns a raw parse tree
  // (or throws an exception).
  Timer timer;
  TaQLNode tree = TaQLNode::parse(str);
  // Now process the raw tree and get the final ParseSelect object.
  try {
    TaQLNodeHandler treeHandler;
    TaQLNodeResult res = treeHandler.handleTree (tree, tempTables);
    const TaQLNodeHRValue& hrval = TaQLNodeHandler::getHR(res);
    commandType = hrval.getString();
    TableExprNode expr = hrval.getExpr();
    if (tree.style().doTiming()) {
      timer.show (" Total time   ");
    }
    if (! expr.isNull()) {
      return TaQLResult(expr);                 // result of CALC command
    }
    //# Copy the possibly selected column names.
    if (hrval.getNames()) {
      Vector<String> tmp(*(hrval.getNames()));
      cols.reference (tmp);
    } else {
      cols.resize (0);
    }
    return hrval.getTable();
  } catch (std::exception& x) {
    throw TableParseError ("'" + str + "'\n  " + x.what());
  }
}

} //# NAMESPACE CASACORE - END
