
/* Module:          results.c
 *
 * Description:     This module contains functions related to 
 *                  retrieving result information through the ODBC API.
 *
 * Classes:         n/a
 *
 * API functions:   SQLRowCount, SQLNumResultCols, SQLDescribeCol, SQLColAttributes,
 *                  SQLGetData, SQLFetch, SQLExtendedFetch, 
 *                  SQLMoreResults(NI), SQLSetPos(NI), SQLSetScrollOptions(NI),
 *                  SQLSetCursorName, SQLGetCursorName
 *
 * Comments:        See "notice.txt" for copyright and license information.
 *
 */

#include <string.h>
#include "psqlodbc.h"
#include "dlg_specific.h"
#include "environ.h"
#include "connection.h"
#include "statement.h"
#include "bind.h"
#include "qresult.h"
#include "convert.h"
#include "pgtypes.h" 

#include <stdio.h>
#include <windows.h>
#include <sqlext.h>

extern GLOBAL_VALUES globals;

RETCODE SQL_API SQLRowCount(
        HSTMT      hstmt,
        SDWORD FAR *pcrow)
{
StatementClass *stmt = (StatementClass *) hstmt;
QResultClass *res;
char *msg, *ptr;

	if ( ! stmt)
		return SQL_ERROR;

	if(stmt->statement_type == STMT_TYPE_SELECT) {
		if (stmt->status == STMT_FINISHED) {
			res = SC_get_Result(stmt);

			if(res && pcrow) {
				*pcrow = globals.use_declarefetch ? 0 : QR_get_num_tuples(res);
				return SQL_SUCCESS;
			}
		}
	} else {

		res = SC_get_Result(stmt);
		if (res && pcrow) {
			msg = QR_get_command(res);
			mylog("*** msg = '%s'\n", msg);
			trim(msg);	//	get rid of trailing spaces
			ptr = strrchr(msg, ' ');
			if (ptr) {
				*pcrow = atoi(ptr+1);
				mylog("**** SQLRowCount(): THE ROWS: *pcrow = %d\n", *pcrow);
			}
			else {
				*pcrow = -1;

				mylog("**** SQLRowCount(): NO ROWS: *pcrow = %d\n", *pcrow);
			}

		return SQL_SUCCESS;
		}
	}

	return SQL_ERROR;     
}


//      This returns the number of columns associated with the database
//      attached to "hstmt".


RETCODE SQL_API SQLNumResultCols(
        HSTMT     hstmt,
        SWORD FAR *pccol)
{       
StatementClass *stmt = (StatementClass *) hstmt;
QResultClass *result;

	if ( ! stmt)
		return SQL_INVALID_HANDLE;

	SC_clear_error(stmt);    

	/* CC: Now check for the "prepared, but not executed" situation, that enables us to
	deal with "SQLPrepare -- SQLDescribeCol -- ... -- SQLExecute" situations.
	(AutoCAD 13 ASE/ASI just _loves_ that ;-) )
	*/
	mylog("**** SQLNumResultCols: calling SC_pre_execute\n");

	SC_pre_execute(stmt);       

	result = SC_get_Result(stmt);

	mylog("SQLNumResultCols: result = %u, status = %d, numcols = %d\n", result, stmt->status, result != NULL ? QR_NumResultCols(result) : -1);
	if (( ! result) || ((stmt->status != STMT_FINISHED) && (stmt->status != STMT_PREMATURE)) ) {
		/* no query has been executed on this statement */
		stmt->errornumber = STMT_SEQUENCE_ERROR;
		stmt->errormsg = "No query has been executed with that handle";
		return SQL_ERROR;
	}

	*pccol = QR_NumResultCols(result);

	return SQL_SUCCESS;
}


//      -       -       -       -       -       -       -       -       -

//      Return information about the database column the user wants
//      information about.
RETCODE SQL_API SQLDescribeCol(
        HSTMT      hstmt,
        UWORD      icol,
        UCHAR  FAR *szColName,
        SWORD      cbColNameMax,
        SWORD  FAR *pcbColName,
        SWORD  FAR *pfSqlType,
        UDWORD FAR *pcbColDef,
        SWORD  FAR *pibScale,
        SWORD  FAR *pfNullable)
{
    /* gets all the information about a specific column */
StatementClass *stmt = (StatementClass *) hstmt;
QResultClass *result;
char *name;
Int4 fieldtype;
int p;
ConnInfo *ci;

    if ( ! stmt)
        return SQL_INVALID_HANDLE;

	ci = &(stmt->hdbc->connInfo);

    SC_clear_error(stmt);

    /* CC: Now check for the "prepared, but not executed" situation, that enables us to
           deal with "SQLPrepare -- SQLDescribeCol -- ... -- SQLExecute" situations.
           (AutoCAD 13 ASE/ASI just _loves_ that ;-) )
    */

	SC_pre_execute(stmt);       

	
    result = SC_get_Result(stmt);
	mylog("**** SQLDescribeCol: result = %u, stmt->status = %d, !finished=%d, !premature=%d\n", result, stmt->status, stmt->status != STMT_FINISHED, stmt->status != STMT_PREMATURE);
    if ( (NULL == result) || ((stmt->status != STMT_FINISHED) && (stmt->status != STMT_PREMATURE))) {
        /* no query has been executed on this statement */
        stmt->errornumber = STMT_SEQUENCE_ERROR;
        stmt->errormsg = "No query has been assigned to this statement.";
        return SQL_ERROR;
    }

    if(icol < 1) {
        // we do not support bookmarks
        stmt->errormsg = "Bookmarks are not currently supported.";
        stmt->errornumber = STMT_NOT_IMPLEMENTED_ERROR;
        return SQL_ERROR;
    }

	icol--;		/* use zero based column numbers */

    if (cbColNameMax >= 1) {
        name = QR_get_fieldname(result, icol);
		mylog("describeCol: col %d fieldname = '%s'\n", icol, name);
        /* our indices start from 0 whereas ODBC defines indices starting from 1 */
        if (NULL != pcbColName)  {
            // we want to get the total number of bytes in the column name
            if (NULL == name) 
                *pcbColName = 0;
            else
                *pcbColName = strlen(name);
        }
        if (NULL != szColName) {
            // get the column name into the buffer if there is one
            if (NULL == name) 
                szColName[0] = '\0';
            else
                strncpy_null(szColName, name, cbColNameMax);
        }
    }

    fieldtype = QR_get_field_type(result, icol);
	mylog("describeCol: col %d fieldtype = %d\n", icol, fieldtype);

    if (NULL != pfSqlType) {
        *pfSqlType = pgtype_to_sqltype(stmt, fieldtype);

		mylog("describeCol: col %d *pfSqlType = %d\n", icol, *pfSqlType);
	}

    if (NULL != pcbColDef) {

		/*	If type is BPCHAR, then precision is length of column because all
			columns in the result set will be blank padded to the column length.

			If type is VARCHAR or TEXT, then precision can not be accurately 
			determined.  Possibilities are:
				1. return 0 (I dont know -- seems to work ok with Borland)
				2. return MAXIMUM PRECISION for that datatype (Borland bad!)
				3. return longest column thus far (that would be the longest 
					strlen of any row in the tuple cache, which may not be a
					good representation if the result set is more than one 
					tuple cache long.)
		*/

		p = pgtype_precision(stmt, fieldtype, icol, globals.unknown_sizes);  // atoi(ci->unknown_sizes)
		if ( p < 0)
			p = 0;		// "I dont know"

		*pcbColDef = p;

		mylog("describeCol: col %d  *pcbColDef = %d\n", icol, *pcbColDef);
	}

    if (NULL != pibScale) {
        Int2 scale;
        scale = pgtype_scale(stmt, fieldtype);
        if(scale == -1) { scale = 0; }
        
        *pibScale = scale;
		mylog("describeCol: col %d  *pibScale = %d\n", icol, *pibScale);
    }

    if (NULL != pfNullable) {
        *pfNullable = pgtype_nullable(stmt, fieldtype);
		mylog("describeCol: col %d  *pfNullable = %d\n", icol, *pfNullable);
    }

    return SQL_SUCCESS;
}

//      Returns result column descriptor information for a result set.

RETCODE SQL_API SQLColAttributes(
        HSTMT      hstmt,
        UWORD      icol,
        UWORD      fDescType,
        PTR        rgbDesc,
        SWORD      cbDescMax,
        SWORD  FAR *pcbDesc,
        SDWORD FAR *pfDesc)
{
StatementClass *stmt = (StatementClass *) hstmt;
char *value;
Int4 field_type;
ConnInfo *ci;
int unknown_sizes;

	if( ! stmt)
		return SQL_INVALID_HANDLE;

	ci = &(stmt->hdbc->connInfo);

    /* CC: Now check for the "prepared, but not executed" situation, that enables us to
           deal with "SQLPrepare -- SQLDescribeCol -- ... -- SQLExecute" situations.
           (AutoCAD 13 ASE/ASI just _loves_ that ;-) )
    */
	SC_pre_execute(stmt);       

	mylog("**** SQLColAtt: result = %u, status = %d, numcols = %d\n", stmt->result, stmt->status, stmt->result != NULL ? QR_NumResultCols(stmt->result) : -1);

	if ( (NULL == stmt->result) || ((stmt->status != STMT_FINISHED) && (stmt->status != STMT_PREMATURE)) ) {
		stmt->errormsg = "Can't get column attributes: no result found.";
		stmt->errornumber = STMT_SEQUENCE_ERROR;
		return SQL_ERROR;
	}

	if(icol < 1) {
		// we do not support bookmarks
		stmt->errormsg = "Bookmarks are not currently supported.";
		stmt->errornumber = STMT_NOT_IMPLEMENTED_ERROR;
		return SQL_ERROR;
	}

	icol -= 1;
	field_type = QR_get_field_type(stmt->result, icol);

	mylog("colAttr: col %d field_type = %d\n", icol, field_type);

	unknown_sizes = globals.unknown_sizes;          // atoi(ci->unknown_sizes);
	if (unknown_sizes == UNKNOWNS_AS_DONTKNOW)		// not appropriate for SQLColAttributes()
		unknown_sizes = UNKNOWNS_AS_MAX;

	switch(fDescType) {
	case SQL_COLUMN_AUTO_INCREMENT:
		if (NULL != pfDesc) {
			*pfDesc = pgtype_auto_increment(stmt, field_type);
			if (*pfDesc == -1)  /*  non-numeric becomes FALSE (ODBC Doc) */
				*pfDesc = FALSE;
				
		}
		break;

	case SQL_COLUMN_CASE_SENSITIVE:
		if (NULL != pfDesc)    
			*pfDesc = pgtype_case_sensitive(stmt, field_type);
		break;

	case SQL_COLUMN_COUNT:
		if (NULL != pfDesc)    
			*pfDesc = QR_NumResultCols(stmt->result);
		break;

    case SQL_COLUMN_DISPLAY_SIZE:
		if (NULL != pfDesc) {
			*pfDesc = pgtype_display_size(stmt, field_type, icol, unknown_sizes);
		}

		mylog("SQLColAttributes: col %d, display_size= %d\n", icol, *pfDesc);

        break;

	case SQL_COLUMN_LABEL:
	case SQL_COLUMN_NAME:
		value = QR_get_fieldname(stmt->result, icol);
		strncpy_null((char *)rgbDesc, value, cbDescMax);

		if (NULL != pcbDesc)
			*pcbDesc = strlen(value);
		break;

	case SQL_COLUMN_LENGTH:
		if (NULL != pfDesc) {
			*pfDesc = pgtype_length(stmt, field_type, icol, unknown_sizes); 
		}
		mylog("SQLColAttributes: col %d, length = %d\n", icol, *pfDesc);
        break;

	case SQL_COLUMN_MONEY:
		if (NULL != pfDesc)    
			*pfDesc = pgtype_money(stmt, field_type);
		break;

	case SQL_COLUMN_NULLABLE:
		if (NULL != pfDesc)    
			*pfDesc = pgtype_nullable(stmt, field_type);
		break;

	case SQL_COLUMN_OWNER_NAME:
		strncpy_null((char *)rgbDesc, "", cbDescMax);
		if (NULL != pcbDesc)        
			*pcbDesc = 0;
		break;

	case SQL_COLUMN_PRECISION:
		if (NULL != pfDesc) {
			*pfDesc = pgtype_precision(stmt, field_type, icol, unknown_sizes);
		}
		mylog("SQLColAttributes: col %d, precision = %d\n", icol, *pfDesc);
        break;

	case SQL_COLUMN_QUALIFIER_NAME:
		strncpy_null((char *)rgbDesc, "", cbDescMax);
		if (NULL != pcbDesc)        
			*pcbDesc = 0;
		break;

	case SQL_COLUMN_SCALE:
		if (NULL != pfDesc)    
			*pfDesc = pgtype_scale(stmt, field_type);
		break;

	case SQL_COLUMN_SEARCHABLE:
		if (NULL != pfDesc)    
			*pfDesc = pgtype_searchable(stmt, field_type);
		break;

    case SQL_COLUMN_TABLE_NAME:
		strncpy_null((char *)rgbDesc, "", cbDescMax);
		if (NULL != pcbDesc)        
			*pcbDesc = 0;
        break;

	case SQL_COLUMN_TYPE:
		if (NULL != pfDesc) {
			*pfDesc = pgtype_to_sqltype(stmt, field_type);
		}
		break;

	case SQL_COLUMN_TYPE_NAME:
		value = pgtype_to_name(stmt, field_type);
		strncpy_null((char *)rgbDesc, value, cbDescMax);
		if (NULL != pcbDesc)        
			*pcbDesc = strlen(value);
		break;

	case SQL_COLUMN_UNSIGNED:
		if (NULL != pfDesc) {
			*pfDesc = pgtype_unsigned(stmt, field_type);
			if(*pfDesc == -1)	/* non-numeric becomes TRUE (ODBC Doc) */
				*pfDesc = TRUE;
		}
		break;

	case SQL_COLUMN_UPDATABLE:
		// everything should be updatable, I guess, unless access permissions
		// prevent it--are we supposed to check for that here?  seems kind
		// of complicated.  hmm...
		if (NULL != pfDesc)    {
			/*	Neither Access or Borland care about this.

			if (field_type == PG_TYPE_OID)
				*pfDesc = SQL_ATTR_READONLY;
			else
			*/
			*pfDesc = SQL_ATTR_WRITE;
		}
		break;
    }

    return SQL_SUCCESS;
}

//      Returns result data for a single column in the current row.

RETCODE SQL_API SQLGetData(
        HSTMT      hstmt,
        UWORD      icol,
        SWORD      fCType,
        PTR        rgbValue,
        SDWORD     cbValueMax,
        SDWORD FAR *pcbValue)
{
QResultClass *res;
StatementClass *stmt = (StatementClass *) hstmt;
int num_cols, num_rows;
Int4 field_type;
void *value;
int result;
char multiple;


mylog("SQLGetData: enter, stmt=%u\n", stmt);

    if( ! stmt) {
        return SQL_INVALID_HANDLE;
    }
	res = stmt->result;

    if (STMT_EXECUTING == stmt->status) {
        stmt->errormsg = "Can't get data while statement is still executing.";
        stmt->errornumber = STMT_SEQUENCE_ERROR;
        return 0;
    }

    if (stmt->status != STMT_FINISHED) {
        stmt->errornumber = STMT_STATUS_ERROR;
        stmt->errormsg = "GetData can only be called after the successful execution on a SQL statement";
        return 0;
    }

    if (icol == 0) {
        stmt->errormsg = "Bookmarks are not currently supported.";
        stmt->errornumber = STMT_NOT_IMPLEMENTED_ERROR;
        return SQL_ERROR;
    }

    // use zero-based column numbers
    icol--;

    // make sure the column number is valid
    num_cols = QR_NumResultCols(res);
    if (icol >= num_cols) {
        stmt->errormsg = "Invalid column number.";
        stmt->errornumber = STMT_INVALID_COLUMN_NUMBER_ERROR;
        return SQL_ERROR;
    }

	if ( stmt->manual_result || ! globals.use_declarefetch) {
		// make sure we're positioned on a valid row
		num_rows = QR_get_num_tuples(res);
		if((stmt->currTuple < 0) ||
		   (stmt->currTuple >= num_rows)) {
			stmt->errormsg = "Not positioned on a valid row for GetData.";
			stmt->errornumber = STMT_INVALID_CURSOR_STATE_ERROR;
			return SQL_ERROR;
		}
		mylog("     num_rows = %d\n", num_rows);
		if ( stmt->manual_result) {
			value = QR_get_value_manual(res, stmt->currTuple, icol);
		}
		else {
			value = QR_get_value_backend_row(res, stmt->currTuple, icol);
		}
		mylog("     value = '%s'\n", value);
	}
	else { /* its a SOCKET result (backend data) */
		if (stmt->currTuple == -1 || ! res || QR_end_tuples(res)) {
			stmt->errormsg = "Not positioned on a valid row for GetData.";
			stmt->errornumber = STMT_INVALID_CURSOR_STATE_ERROR;
			return SQL_ERROR;
		}

		value = QR_get_value_backend(res, icol);

		mylog("  socket: value = '%s'\n", value);
	}

	field_type = QR_get_field_type(res, icol);

	mylog("**** SQLGetData: icol = %d, fCType = %d, field_type = %d, value = '%s'\n", icol, fCType, field_type, value);

	/*	Is this another call for the same column to retrieve more data? */
	multiple = (icol == stmt->current_col) ? TRUE : FALSE;

    result = copy_and_convert_field(stmt, field_type, value, 
                                    fCType, rgbValue, cbValueMax, pcbValue, multiple);


	stmt->current_col = icol;

	switch(result) {
	case COPY_OK:
		return SQL_SUCCESS;

	case COPY_UNSUPPORTED_TYPE:
		stmt->errormsg = "Received an unsupported type from Postgres.";
		stmt->errornumber = STMT_RESTRICTED_DATA_TYPE_ERROR;
		return SQL_ERROR;

	case COPY_UNSUPPORTED_CONVERSION:
		stmt->errormsg = "Couldn't handle the necessary data type conversion.";
		stmt->errornumber = STMT_RESTRICTED_DATA_TYPE_ERROR;
		return SQL_ERROR;

	case COPY_RESULT_TRUNCATED:
		stmt->errornumber = STMT_TRUNCATED;
		stmt->errormsg = "The buffer was too small for the result.";
		return SQL_SUCCESS_WITH_INFO;

	case COPY_GENERAL_ERROR:	/* error msg already filled in */
		return SQL_ERROR;

	case COPY_NO_DATA_FOUND:
		return SQL_NO_DATA_FOUND;

    default:
        stmt->errormsg = "Unrecognized return value from copy_and_convert_field.";
        stmt->errornumber = STMT_INTERNAL_ERROR;
        return SQL_ERROR;
    }
}

//      Returns data for bound columns in the current row ("hstmt->iCursor"),
//      advances the cursor.

RETCODE SQL_API SQLFetch(
        HSTMT   hstmt)
{
StatementClass *stmt = (StatementClass *) hstmt;   
QResultClass *res;
int retval;
Int2 num_cols, lf;
Oid type;
char *value;
ColumnInfoClass *ci;
// TupleField *tupleField;

	if ( ! stmt)
		return SQL_INVALID_HANDLE;

	SC_clear_error(stmt);

	if ( ! (res = stmt->result)) {
		stmt->errormsg = "Null statement result in SQLFetch.";
		stmt->errornumber = STMT_SEQUENCE_ERROR;
		return SQL_ERROR;
	}

	ci = QR_get_fields(res);		/* the column info */

	if (stmt->status == STMT_EXECUTING) {
		stmt->errormsg = "Can't fetch while statement is still executing.";
		stmt->errornumber = STMT_SEQUENCE_ERROR;
		return SQL_ERROR;
	}


	if (stmt->status != STMT_FINISHED) {
		stmt->errornumber = STMT_STATUS_ERROR;
		stmt->errormsg = "Fetch can only be called after the successful execution on a SQL statement";
		return SQL_ERROR;
	}

	if (stmt->bindings == NULL) {
		// just to avoid a crash if the user insists on calling this
		// function even if SQL_ExecDirect has reported an Error
		stmt->errormsg = "Bindings were not allocated properly.";
		stmt->errornumber = STMT_SEQUENCE_ERROR;
		return SQL_ERROR;
	}

	mylog("manual_result = %d, use_declarefetch = %d\n",
		stmt->manual_result, globals.use_declarefetch);
 
	if ( stmt->manual_result || ! globals.use_declarefetch) {

		if (stmt->currTuple >= QR_get_num_tuples(res) -1 || 
			(stmt->maxRows > 0 && stmt->currTuple == stmt->maxRows - 1)) {

			/*	if at the end of the tuples, return "no data found" 
				and set the cursor past the end of the result set 
			*/
			stmt->currTuple = QR_get_num_tuples(res);	
			return SQL_NO_DATA_FOUND;
		}
 
		mylog("**** SQLFetch: manual_result\n");
		(stmt->currTuple)++;
	}
	else {

		// read from the cache or the physical next tuple
		retval = QR_next_tuple(res);
		if (retval < 0) {
			mylog("**** SQLFetch: end_tuples\n");
			return SQL_NO_DATA_FOUND;
		}
		else if (retval > 0)
			(stmt->currTuple)++;		// all is well

		else {
			mylog("SQLFetch: error\n");
			stmt->errornumber = STMT_EXEC_ERROR;
			stmt->errormsg = "Error fetching next row";
			return SQL_ERROR;
		}
	}

	num_cols = QR_NumResultCols(res);

	for (lf=0; lf < num_cols; lf++) {

		mylog("fetch: cols=%d, lf=%d, stmt = %u, stmt->bindings = %u, buffer[] = %u\n", 
			 num_cols, lf, stmt, stmt->bindings, stmt->bindings[lf].buffer);

		if (stmt->bindings[lf].buffer != NULL) {
            // this column has a binding

            // type = QR_get_field_type(res, lf);
			type = CI_get_oid(ci, lf);		/* speed things up */

			mylog("type = %d\n", type);

			if (stmt->manual_result)
				value = QR_get_value_manual(res, stmt->currTuple, lf);
			else if (globals.use_declarefetch)
				value = QR_get_value_backend(res, lf);
			else {
				value = QR_get_value_backend_row(res, stmt->currTuple, lf);
			}

			mylog("value = '%s'\n", value);

			retval = copy_and_convert_field_bindinfo(stmt, type, value, lf);

			mylog("copy_and_convert: retval = %d\n", retval);

			// check whether the complete result was copied
			if(retval == COPY_UNSUPPORTED_TYPE) {
				stmt->errormsg = "Received an unsupported type from Postgres.";
				stmt->errornumber = STMT_RESTRICTED_DATA_TYPE_ERROR;
				return SQL_ERROR;

			} else if(retval == COPY_UNSUPPORTED_CONVERSION) {
				stmt->errormsg = "Couldn't handle the necessary data type conversion.";
				stmt->errornumber = STMT_RESTRICTED_DATA_TYPE_ERROR;
				return SQL_ERROR;

			} else if(retval == COPY_RESULT_TRUNCATED) {
				/* The result has been truncated during the copy */
				/* this will generate a SQL_SUCCESS_WITH_INFO result */
				stmt->errornumber = STMT_TRUNCATED;
				stmt->errormsg = "A buffer was too small for the return value to fit in";
				return SQL_SUCCESS_WITH_INFO;

			} else if(retval != COPY_OK) {
				stmt->errormsg = "Unrecognized return value from copy_and_convert_field.";
				stmt->errornumber = STMT_INTERNAL_ERROR;
				return SQL_ERROR;

			}
		}
	}

	return SQL_SUCCESS;
}

//      This fetchs a block of data (rowset).

RETCODE SQL_API SQLExtendedFetch(
        HSTMT      hstmt,
        UWORD      fFetchType,
        SDWORD     irow,
        UDWORD FAR *pcrow,
        UWORD  FAR *rgfRowStatus)
{
StatementClass *stmt = (StatementClass *) hstmt;
int num_tuples;
RETCODE result;


mylog("SQLExtendedFetch: stmt=%u\n", stmt);

	if ( ! stmt)
		return SQL_INVALID_HANDLE;

	if ( globals.use_declarefetch)
		return SQL_ERROR;

	/*	Initialize to no rows fetched */
	if (rgfRowStatus)
		*rgfRowStatus = SQL_ROW_NOROW;
	if (pcrow)
		*pcrow = 0;

	num_tuples = QR_get_num_tuples(stmt->result);

	switch (fFetchType)  {
	case SQL_FETCH_NEXT:
		mylog("SQL_FETCH_NEXT: num_tuples=%d, currtuple=%d\n", num_tuples, stmt->currTuple);
		break;

	case SQL_FETCH_PRIOR:
		mylog("SQL_FETCH_PRIOR: num_tuples=%d, currtuple=%d\n", num_tuples, stmt->currTuple);

		/*	If already before result set, return no data found */
		if (stmt->currTuple <= 0)
			return SQL_NO_DATA_FOUND;

		stmt->currTuple -= 2;
		break;

	case SQL_FETCH_FIRST:
		mylog("SQL_FETCH_FIRST: num_tuples=%d, currtuple=%d\n", num_tuples, stmt->currTuple);

		stmt->currTuple = -1;
		break;

	case SQL_FETCH_LAST:
		mylog("SQL_FETCH_LAST: num_tuples=%d, currtuple=%d\n", num_tuples, stmt->currTuple);
		stmt->currTuple = num_tuples <= 0 ? -1 : (num_tuples - 2);
		break;

	case SQL_FETCH_ABSOLUTE:
		mylog("SQL_FETCH_ABSOLUTE: num_tuples=%d, currtuple=%d, irow=%d\n", num_tuples, stmt->currTuple, irow);

		/*	Position before result set, but dont fetch anything */
		if (irow == 0) {
			stmt->currTuple = -1;
			return SQL_NO_DATA_FOUND;
		}
		/*	Position before the desired row */
		else if (irow > 0) {
			stmt->currTuple = irow-2;
		}
		/*	Position with respect to the end of the result set */
		else {
			stmt->currTuple = num_tuples + irow - 1;
		}    
		break;

	default:
		return SQL_ERROR;   

	}           

	mylog("SQLExtendedFetch: new currTuple = %d\n", stmt->currTuple);

	result = SQLFetch(hstmt);

	if (result == SQL_SUCCESS) {
		if (rgfRowStatus)
			*rgfRowStatus = SQL_ROW_SUCCESS;
		if (pcrow)
			*pcrow = 1;
	}

	return result;

}


//      This determines whether there are more results sets available for
//      the "hstmt".

/* CC: return SQL_NO_DATA_FOUND since we do not support multiple result sets */
RETCODE SQL_API SQLMoreResults(
        HSTMT   hstmt)
{
          return SQL_NO_DATA_FOUND;
}

//      This positions the cursor within a block of data.

RETCODE SQL_API SQLSetPos(
        HSTMT   hstmt,
        UWORD   irow,
        UWORD   fOption,
        UWORD   fLock)
{
        return SQL_ERROR;
}

//      Sets options that control the behavior of cursors.

RETCODE SQL_API SQLSetScrollOptions(
        HSTMT      hstmt,
        UWORD      fConcurrency,
        SDWORD  crowKeyset,
        UWORD      crowRowset)
{
        return SQL_ERROR;
}


//      Set the cursor name on a statement handle

RETCODE SQL_API SQLSetCursorName(
        HSTMT     hstmt,
        UCHAR FAR *szCursor,
        SWORD     cbCursor)
{
StatementClass *stmt = (StatementClass *) hstmt;
int len;

mylog("SQLSetCursorName: hstmt=%u, szCursor=%u, cbCursorMax=%d\n",
	  hstmt, szCursor, cbCursor);

	if ( ! stmt)
		return SQL_INVALID_HANDLE;

	len = (cbCursor == SQL_NTS) ? strlen(szCursor) : cbCursor;
	mylog("cursor len = %d\n", len);
	if (len <= 0 || len > sizeof(stmt->cursor_name) - 1) {
		stmt->errornumber = STMT_INVALID_CURSOR_NAME;
		stmt->errormsg = "Invalid Cursor Name";
		return SQL_ERROR;
	}
	strncpy_null(stmt->cursor_name, szCursor, cbCursor);
	return SQL_SUCCESS;
}

//      Return the cursor name for a statement handle

RETCODE SQL_API SQLGetCursorName(
        HSTMT     hstmt,
        UCHAR FAR *szCursor,
        SWORD     cbCursorMax,
        SWORD FAR *pcbCursor)
{
StatementClass *stmt = (StatementClass *) hstmt;

mylog("SQLGetCursorName: hstmt=%u, szCursor=%u, cbCursorMax=%d, pcbCursor=%u\n",
	  hstmt, szCursor, cbCursorMax, pcbCursor);

	if ( ! stmt)
		return SQL_INVALID_HANDLE;


	if ( stmt->cursor_name[0] == '\0') {
		stmt->errornumber = STMT_NO_CURSOR_NAME;
		stmt->errormsg = "No Cursor name available";
		return SQL_ERROR;
	}

	strncpy_null(szCursor, stmt->cursor_name, cbCursorMax);

	if (pcbCursor)
		*pcbCursor = strlen(szCursor);

	return SQL_SUCCESS;
}


