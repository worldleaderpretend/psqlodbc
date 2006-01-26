/*
 * Module:			results.c
 *
 * Description:		This module contains functions related to
 *					retrieving result information through the ODBC API.
 *
 * Classes:			n/a
 *
 * API functions:	SQLRowCount, SQLNumResultCols, SQLDescribeCol,
 *					SQLColAttributes, SQLGetData, SQLFetch, SQLExtendedFetch,
 *					SQLMoreResults, SQLSetPos, SQLSetScrollOptions(NI),
 *					SQLSetCursorName, SQLGetCursorName
 *
 * Comments:		See "notice.txt" for copyright and license information.
 *-------
 */

#include "psqlodbc.h"

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
#include <limits.h>

#include "pgapifunc.h"



RETCODE		SQL_API
PGAPI_RowCount(
			   HSTMT hstmt,
			   SQLLEN FAR * pcrow)
{
	CSTR func = "PGAPI_RowCount";
	StatementClass *stmt = (StatementClass *) hstmt;
	QResultClass *res;
	ConnInfo   *ci;

	mylog("%s: entering...\n", func);
	if (!stmt)
	{
		SC_log_error(func, "", NULL);
		return SQL_INVALID_HANDLE;
	}
	ci = &(SC_get_conn(stmt)->connInfo);
	if (stmt->proc_return > 0)
	{
		if (pcrow)
{
			*pcrow = 0;
inolog("returning RowCount=%d\n", *pcrow);
}
		return SQL_SUCCESS;
	}

	res = SC_get_Curres(stmt);
	if (res && pcrow)
	{
		if (stmt->status != STMT_FINISHED)
		{
			SC_set_error(stmt, STMT_SEQUENCE_ERROR, "Can't get row count while statement is still executing.", func);
			return	SQL_ERROR;
		}
		if (res->recent_processed_row_count >= 0)
		{
			*pcrow = res->recent_processed_row_count;
			mylog("**** PGAPI_RowCount(): THE ROWS: *pcrow = %d\n", *pcrow);

			return SQL_SUCCESS;
		}
		else if (QR_NumResultCols(res) > 0)
		{
			*pcrow = SC_is_fetchcursor(stmt) ? -1 : QR_get_num_total_tuples(res) - res->dl_count;
			mylog("RowCount=%d\n", *pcrow);
			return SQL_SUCCESS;
		}
	}

	*pcrow = -1;
	return SQL_SUCCESS;
}


/*
 *	This returns the number of columns associated with the database
 *	attached to "hstmt".
 */
RETCODE		SQL_API
PGAPI_NumResultCols(
					HSTMT hstmt,
					SQLSMALLINT FAR * pccol)
{
	CSTR func = "PGAPI_NumResultCols";
	StatementClass *stmt = (StatementClass *) hstmt;
	QResultClass *result;
	char		parse_ok;
	ConnInfo   *ci;
	RETCODE		ret = SQL_SUCCESS;

	mylog("%s: entering...\n", func);
	if (!stmt)
	{
		SC_log_error(func, "", NULL);
		return SQL_INVALID_HANDLE;
	}
	ci = &(SC_get_conn(stmt)->connInfo);

	SC_clear_error(stmt);
#define	return	DONT_CALL_RETURN_FROM_HERE???
	/* StartRollbackState(stmt); */

	if (stmt->proc_return > 0)
	{
		*pccol = 0;
		goto cleanup;
	}
	parse_ok = FALSE;
	if (!stmt->catalog_result && ci->drivers.parse && stmt->statement_type == STMT_TYPE_SELECT)
	{
		if (SC_parsed_status(stmt) == STMT_PARSE_NONE)
		{
			mylog("PGAPI_NumResultCols: calling parse_statement on stmt=%x\n", stmt);
			parse_statement(stmt, FALSE);
		}

		if (SC_parsed_status(stmt) != STMT_PARSE_FATAL)
		{
			parse_ok = TRUE;
			*pccol = SC_get_IRDF(stmt)->nfields;
			mylog("PARSE: PGAPI_NumResultCols: *pccol = %d\n", *pccol);
		}
	}

	if (!parse_ok)
	{
		Int4 num_fields = SC_pre_execute(stmt);
		result = SC_get_Curres(stmt);

		mylog("PGAPI_NumResultCols: result = %x, status = %d, numcols = %d\n", result, stmt->status, result != NULL ? QR_NumResultCols(result) : -1);
		/****if ((!result) || ((stmt->status != STMT_FINISHED) && (stmt->status != STMT_PREMATURE))) ****/
		if (!result || num_fields < 0)
		{
			/* no query has been executed on this statement */
			SC_set_error(stmt, STMT_EXEC_ERROR, "No query has been executed with that handle", func);
			ret = SQL_ERROR;
			goto cleanup;
		}
		else if (!QR_command_maybe_successful(result))
		{
			SC_set_errornumber(stmt, STMT_EXEC_ERROR);
			SC_log_error(func, "", stmt);
			ret = SQL_ERROR;
			goto cleanup;
		}

		*pccol = QR_NumPublicResultCols(result);
	}

cleanup:
#undef	return
	if (stmt->internal)
		ret = DiscardStatementSvp(stmt, ret, FALSE);
	return ret;
}


/*
 *	Return information about the database column the user wants
 *	information about.
 */
RETCODE		SQL_API
PGAPI_DescribeCol(
				  HSTMT hstmt,
				  SQLUSMALLINT icol,
				  SQLCHAR FAR * szColName,
				  SQLSMALLINT cbColNameMax,
				  SQLSMALLINT FAR * pcbColName,
				  SQLSMALLINT FAR * pfSqlType,
				  SQLULEN FAR * pcbColDef,
				  SQLSMALLINT FAR * pibScale,
				  SQLSMALLINT FAR * pfNullable)
{
	CSTR func = "PGAPI_DescribeCol";

	/* gets all the information about a specific column */
	StatementClass *stmt = (StatementClass *) hstmt;
	ConnectionClass *conn;
	IRDFields	*irdflds;
	QResultClass *res;
	char	   *col_name = NULL;
	Int4		fieldtype = 0;
	SQLULEN		column_size = 0;
	SQLINTEGER	decimal_digits = 0;
	ConnInfo   *ci;
	char		parse_ok;
	FIELD_INFO	*fi;
	char		buf[255];
	int			len = 0;
	RETCODE		result = SQL_SUCCESS;

	mylog("%s: entering.%d..\n", func, icol);

	if (!stmt)
	{
		SC_log_error(func, "", NULL);
		return SQL_INVALID_HANDLE;
	}

	conn = SC_get_conn(stmt);
	ci = &(conn->connInfo);

	SC_clear_error(stmt);

#define	return	DONT_CALL_RETURN_FROM_HERE???
	/* StartRollbackState(stmt); */
	irdflds = SC_get_IRDF(stmt);
#if (ODBCVER >= 0x0300)
	if (0 == icol) /* bookmark column */
	{
		SQLSMALLINT	fType = stmt->options.use_bookmarks == SQL_UB_VARIABLE ? SQL_BINARY : SQL_INTEGER;

inolog("answering bookmark info\n");
		if (szColName && cbColNameMax > 0)
			*szColName = '\0';
		if (pcbColName)
			*pcbColName = 0;
		if (pfSqlType)
			*pfSqlType = fType;
		if (pcbColDef)
			*pcbColDef = 10;
		if (pibScale)
			*pibScale = 0;
		if (pfNullable)
			*pfNullable = SQL_NO_NULLS;
		result = SQL_SUCCESS;
		goto cleanup;
	}
#endif /* ODBCVER */
	/*
	 * Dont check for bookmark column. This is the responsibility of the
	 * driver manager.
	 */

	icol--;						/* use zero based column numbers */

	parse_ok = FALSE;
	fi = NULL;
	if (icol < irdflds->nfields && irdflds->fi)
	{
		fi = irdflds->fi[icol];
		if (fi && 0 == (fi->flag & (FIELD_PARSED_OK | FIELD_COL_ATTRIBUTE)))
			fi = NULL;
	}
	if (!fi && !stmt->catalog_result && ci->drivers.parse && stmt->statement_type == STMT_TYPE_SELECT)
	{
		if (SC_parsed_status(stmt) == STMT_PARSE_NONE)
		{
			mylog("PGAPI_DescribeCol: calling parse_statement on stmt=%x\n", stmt);
			parse_statement(stmt, FALSE);
		}

		mylog("PARSE: DescribeCol: icol=%d, stmt=%x, stmt->nfld=%d, stmt->fi=%x\n", icol, stmt, irdflds->nfields, irdflds->fi);

		if (SC_parsed_status(stmt) != STMT_PARSE_FATAL && irdflds->fi)
		{
			if (icol < irdflds->nfields)
				fi = irdflds->fi[icol];
			else
			{
				SC_set_error(stmt, STMT_INVALID_COLUMN_NUMBER_ERROR, "Invalid column number in DescribeCol.", func);
				result = SQL_ERROR;
				goto cleanup;
			}
			mylog("DescribeCol: getting info for icol=%d\n", icol);
		}
	}

	if (fi && 0 == fi->type)
		fi = NULL;

	/*
	 * If couldn't parse it OR the field being described was not parsed
	 * (i.e., because it was a function or expression, etc, then do it the
	 * old fashioned way.
	 */
	if (!fi)
	{
		Int4 num_fields = SC_pre_execute(stmt);

		res = SC_get_Curres(stmt);

		mylog("**** PGAPI_DescribeCol: res = %x, stmt->status = %d, !finished=%d, !premature=%d\n", res, stmt->status, stmt->status != STMT_FINISHED, stmt->status != STMT_PREMATURE);
		/**** if ((NULL == res) || ((stmt->status != STMT_FINISHED) && (stmt->status != STMT_PREMATURE))) ****/
		if ((NULL == res) || num_fields < 0)
		{
			/* no query has been executed on this statement */
			SC_set_error(stmt, STMT_EXEC_ERROR, "No query has been assigned to this statement.", func);
			result = SQL_ERROR;
			goto cleanup;
		}
		else if (!QR_command_maybe_successful(res))
		{
			SC_set_errornumber(stmt, STMT_EXEC_ERROR);
			SC_log_error(func, "", stmt);
			result = SQL_ERROR;
			goto cleanup;
		}

		if (icol >= QR_NumPublicResultCols(res))
		{
			SC_set_error(stmt, STMT_INVALID_COLUMN_NUMBER_ERROR, "Invalid column number in DescribeCol.", NULL);
			snprintf(buf, sizeof(buf), "Col#=%d, #Cols=%d,%d keys=%d", icol, QR_NumResultCols(res), QR_NumPublicResultCols(res), res->num_key_fields);
			SC_log_error(func, buf, stmt);
			result = SQL_ERROR;
			goto cleanup;
		}
	}
	if (fi)
	{
		fieldtype = fi->type;
		if (NAME_IS_VALID(fi->column_alias))
			col_name = GET_NAME(fi->column_alias);
		else
			col_name = GET_NAME(fi->column_name);
		column_size = fi->column_size;
		decimal_digits = fi->decimal_digits;

		mylog("PARSE: fieldtype=%d, col_name='%s', column_size=%d\n", fieldtype, col_name, column_size);
	}		
	else
	{
		col_name = QR_get_fieldname(res, icol);
		fieldtype = QR_get_field_type(res, icol);

		/* atoi(ci->unknown_sizes) */
		column_size = pgtype_column_size(stmt, fieldtype, icol, ci->drivers.unknown_sizes);
		decimal_digits = pgtype_decimal_digits(stmt, fieldtype, icol);
	}

	mylog("describeCol: col %d fieldname = '%s'\n", icol, col_name);
	mylog("describeCol: col %d fieldtype = %d\n", icol, fieldtype);
	mylog("describeCol: col %d column_size = %d\n", icol, column_size);

	result = SQL_SUCCESS;

	/*
	 * COLUMN NAME
	 */
	len = strlen(col_name);

	if (pcbColName)
		*pcbColName = len;

	if (szColName && cbColNameMax > 0)
	{
		strncpy_null(szColName, col_name, cbColNameMax);

		if (len >= cbColNameMax)
		{
			result = SQL_SUCCESS_WITH_INFO;
			SC_set_error(stmt, STMT_TRUNCATED, "The buffer was too small for the colName.", func);
		}
	}

	/*
	 * CONCISE(SQL) TYPE
	 */
	if (pfSqlType)
	{
		*pfSqlType = pgtype_to_concise_type(stmt, fieldtype, icol);

		mylog("describeCol: col %d *pfSqlType = %d\n", icol, *pfSqlType);
	}

	/*
	 * COLUMN SIZE(PRECISION in 2.x)
	 */
	if (pcbColDef)
	{
		if (column_size < 0)
			column_size = 0;		/* "I dont know" */

		*pcbColDef = column_size;

		mylog("describeCol: col %d  *pcbColDef = %d\n", icol, *pcbColDef);
	}

	/*
	 * DECIMAL DIGITS(SCALE in 2.x)
	 */
	if (pibScale)
	{
		if (decimal_digits < 0)
			decimal_digits = 0;

		*pibScale = (SQLSMALLINT) decimal_digits;
		mylog("describeCol: col %d  *pibScale = %d\n", icol, *pibScale);
	}

	/*
	 * NULLABILITY
	 */
	if (pfNullable)
	{
		*pfNullable = (parse_ok) ? irdflds->fi[icol]->nullable : pgtype_nullable(stmt, fieldtype);

		mylog("describeCol: col %d  *pfNullable = %d\n", icol, *pfNullable);
	}

cleanup:
#undef	return
	if (stmt->internal)
		result = DiscardStatementSvp(stmt, result, FALSE);
	return result;
}


/*		Returns result column descriptor information for a result set. */
RETCODE		SQL_API
PGAPI_ColAttributes(
					HSTMT hstmt,
					SQLUSMALLINT icol,
					SQLUSMALLINT fDescType,
					PTR rgbDesc,
					SQLSMALLINT cbDescMax,
					SQLSMALLINT FAR * pcbDesc,
					SQLLEN FAR * pfDesc)
{
	CSTR func = "PGAPI_ColAttributes";
	StatementClass *stmt = (StatementClass *) hstmt;
	IRDFields	*irdflds;
	Int4		col_idx, field_type = 0;
	ConnectionClass	*conn;
	ConnInfo	*ci;
	int			unknown_sizes;
	int			cols = 0;
	char		parse_ok;
	RETCODE		result;
	const char   *p = NULL;
	int			len = 0,
				value = 0;
	const	FIELD_INFO	*fi = NULL;
	const	TABLE_INFO	*ti = NULL;

	mylog("%s: entering..col=%d %d len=%d.\n", func, icol, fDescType,
				cbDescMax);

	if (!stmt)
	{
		SC_log_error(func, "", NULL);
		return SQL_INVALID_HANDLE;
	}

	if (pcbDesc)
		*pcbDesc = 0;
	irdflds = SC_get_IRDF(stmt);
	conn = SC_get_conn(stmt);
	ci = &(conn->connInfo);

	/*
	 * Dont check for bookmark column.	This is the responsibility of the
	 * driver manager.	For certain types of arguments, the column number
	 * is ignored anyway, so it may be 0.
	 */

#if (ODBCVER >= 0x0300)
	if (0 == icol && SQL_DESC_COUNT != fDescType) /* bookmark column */
	{
inolog("answering bookmark info\n");
		switch (fDescType)
		{
			case SQL_DESC_OCTET_LENGTH:
				if (pfDesc)
					*pfDesc = 4;
				break;
			case SQL_DESC_TYPE:
				if (pfDesc)
					*pfDesc = stmt->options.use_bookmarks == SQL_UB_VARIABLE ? SQL_BINARY : SQL_INTEGER;
				break;
		}
		return SQL_SUCCESS;
	}
#endif /* ODBCVER */
	col_idx = icol - 1;

	/* atoi(ci->unknown_sizes); */
	unknown_sizes = ci->drivers.unknown_sizes;

	/* not appropriate for SQLColAttributes() */
	if (unknown_sizes == UNKNOWNS_AS_DONTKNOW)
		unknown_sizes = UNKNOWNS_AS_MAX;

	parse_ok = FALSE;
	if (!stmt->catalog_result && ci->drivers.parse && stmt->statement_type == STMT_TYPE_SELECT)
	{
		if (SC_parsed_status(stmt) == STMT_PARSE_NONE)
		{
			mylog("%s: calling parse_statement\n", func);
			parse_statement(stmt, FALSE);
		}

		cols = irdflds->nfields;

		/*
		 * Column Count is a special case.	The Column number is ignored
		 * in this case.
		 */
#if (ODBCVER >= 0x0300)
		if (fDescType == SQL_DESC_COUNT)
#else
		if (fDescType == SQL_COLUMN_COUNT)
#endif /* ODBCVER */
		{
			if (pfDesc)
				*pfDesc = cols;

			return SQL_SUCCESS;
		}

		if (SC_parsed_status(stmt) != STMT_PARSE_FATAL && irdflds->fi)
		{
			if (col_idx >= cols)
			{
				SC_set_error(stmt, STMT_INVALID_COLUMN_NUMBER_ERROR, "Invalid column number in ColAttributes.", func);
				return SQL_ERROR;
			}
			if (irdflds->fi[col_idx])
			{
				field_type = irdflds->fi[col_idx]->type;
				if (field_type > 0)
					parse_ok = TRUE;
			}
		}
	}

	if (col_idx < irdflds->nfields && irdflds->fi)
		fi = irdflds->fi[col_idx];
	if (fi && 0 != (fi->flag & (FIELD_COL_ATTRIBUTE | FIELD_PARSED_OK)))
		;
	else
	{
		fi = NULL;
		SC_pre_execute(stmt);

		mylog("**** PGAPI_ColAtt: result = %x, status = %d, numcols = %d\n", SC_get_Curres(stmt), stmt->status, SC_get_Curres(stmt) != NULL ? QR_NumResultCols(SC_get_Curres(stmt)) : -1);

		if ((NULL == SC_get_Curres(stmt)) || ((stmt->status != STMT_FINISHED) && (stmt->status != STMT_PREMATURE)))
		{
			SC_set_error(stmt, STMT_EXEC_ERROR, "Can't get column attributes: no result found.", func);
			return SQL_ERROR;
		}

		cols = QR_NumPublicResultCols(SC_get_Curres(stmt));

		/*
		 * Column Count is a special case.	The Column number is ignored
		 * in this case.
		 */
#if (ODBCVER >= 0x0300)
		if (fDescType == SQL_DESC_COUNT)
#else
		if (fDescType == SQL_COLUMN_COUNT)
#endif /* ODBCVER */
		{
			if (pfDesc)
				*pfDesc = cols;

			return SQL_SUCCESS;
		}

		if (col_idx >= cols)
		{
			SC_set_error(stmt, STMT_INVALID_COLUMN_NUMBER_ERROR, "Invalid column number in ColAttributes.", func);
			return SQL_ERROR;
		}

		field_type = QR_get_field_type(SC_get_Curres(stmt), col_idx);
		if (SC_parsed_status(stmt) != STMT_PARSE_FATAL && irdflds->fi && col_idx < irdflds->nfields)
			fi = irdflds->fi[col_idx];
	}
	if (fi)
	{
		ti = fi->ti;
		field_type = fi->type;
	}

	mylog("colAttr: col %d field_type = %d\n", col_idx, field_type);

	switch (fDescType)
	{
		case SQL_COLUMN_AUTO_INCREMENT: /* == SQL_DESC_AUTO_UNIQUE_VALUE */
			if (fi && fi->auto_increment)
				value = TRUE;
			else
				value = pgtype_auto_increment(stmt, field_type);
			if (value == -1)	/* non-numeric becomes FALSE (ODBC Doc) */
				value = FALSE;
inolog("AUTO_INCREMENT=%d\n", value);

			break;

		case SQL_COLUMN_CASE_SENSITIVE: /* == SQL_DESC_CASE_SENSITIVE */
			value = pgtype_case_sensitive(stmt, field_type);
			break;

			/*
			 * This special case is handled above.
			 *
			 * case SQL_COLUMN_COUNT:
			 */
		case SQL_COLUMN_DISPLAY_SIZE: /* == SQL_DESC_DISPLAY_SIZE */
			value = (fi && 0 != fi->display_size) ? fi->display_size : pgtype_display_size(stmt, field_type, col_idx, unknown_sizes);

			mylog("%s: col %d, display_size= %d\n", func, col_idx, value);

			break;

		case SQL_COLUMN_LABEL: /* == SQL_DESC_LABEL */
			if (fi && (NAME_IS_VALID(fi->column_alias)))
			{
				p = GET_NAME(fi->column_alias);

				mylog("%s: COLUMN_LABEL = '%s'\n", func, p);
				break;
			}
			/* otherwise same as column name -- FALL THROUGH!!! */

#if (ODBCVER >= 0x0300)
		case SQL_DESC_NAME:
#else
		case SQL_COLUMN_NAME:
#endif /* ODBCVER */
inolog("fi=%x", fi);
if (fi)
inolog(" (%s,%s)", PRINT_NAME(fi->column_alias), PRINT_NAME(fi->column_name));
			p = fi ? (NAME_IS_NULL(fi->column_alias) ? SAFE_NAME(fi->column_name) : GET_NAME(fi->column_alias)) : QR_get_fieldname(SC_get_Curres(stmt), col_idx);

			mylog("%s: COLUMN_NAME = '%s'\n", func, p);
			break;

		case SQL_COLUMN_LENGTH:
			value = (fi && fi->length > 0) ? fi->length : pgtype_buffer_length(stmt, field_type, col_idx, unknown_sizes);
			if (value < 0)
				value = 0;

			mylog("%s: col %d, length = %d\n", func, col_idx, value);
			break;

		case SQL_COLUMN_MONEY: /* == SQL_DESC_FIXED_PREC_SCALE */
			value = pgtype_money(stmt, field_type);
inolog("COLUMN_MONEY=%d\n", value);
			break;

#if (ODBCVER >= 0x0300)
		case SQL_DESC_NULLABLE:
#else
		case SQL_COLUMN_NULLABLE:
#endif /* ODBCVER */
			value = fi ? fi->nullable : pgtype_nullable(stmt, field_type);
inolog("COLUMN_NULLABLE=%d\n", value);
			break;

		case SQL_COLUMN_OWNER_NAME: /* == SQL_DESC_SCHEMA_NAME */
			p = ti ? SAFE_NAME(ti->schema_name) : NULL_STRING;
			break;

		case SQL_COLUMN_PRECISION: /* in 2.x */
			value = (fi && fi->column_size > 0) ? fi->column_size : pgtype_column_size(stmt, field_type, col_idx, unknown_sizes);
			if (value < 0)
				value = 0;

			mylog("%s: col %d, column_size = %d\n", func, col_idx, value);
			break;

		case SQL_COLUMN_QUALIFIER_NAME: /* == SQL_DESC_CATALOG_NAME */
			p = "";
			break;

		case SQL_COLUMN_SCALE: /* in 2.x */
			value = pgtype_decimal_digits(stmt, field_type, col_idx);
inolog("COLUMN_SCALE=%d\n", value);
			if (value < 0)
				value = 0;
			break;

		case SQL_COLUMN_SEARCHABLE: /* SQL_DESC_SEARCHABLE */
			value = pgtype_searchable(stmt, field_type);
			break;

		case SQL_COLUMN_TABLE_NAME: /* == SQL_DESC_TABLE_NAME */
			p = ti ? SAFE_NAME(ti->table_name) : NULL_STRING;

			mylog("%sr: TABLE_NAME = '%s'\n", func, p);
			break;

		case SQL_COLUMN_TYPE: /* == SQL_DESC_CONCISE_TYPE */
			value = pgtype_to_concise_type(stmt, field_type, col_idx);
inolog("COLUMN_TYPE=%d\n", value);
			break;

		case SQL_COLUMN_TYPE_NAME: /* == SQL_DESC_TYPE_NAME */
			p = pgtype_to_name(stmt, field_type);
			break;

		case SQL_COLUMN_UNSIGNED: /* == SQL_DESC_UNSINGED */
			value = pgtype_unsigned(stmt, field_type);
			if (value == -1)	/* non-numeric becomes TRUE (ODBC Doc) */
				value = TRUE;

			break;

		case SQL_COLUMN_UPDATABLE: /* == SQL_DESC_UPDATABLE */

			/*
			 * Neither Access or Borland care about this.
			 *
			 * if (field_type == PG_TYPE_OID) pfDesc = SQL_ATTR_READONLY;
			 * else
			 */
			value = fi ? (fi->updatable ? SQL_ATTR_WRITE : SQL_ATTR_READONLY) : SQL_ATTR_READWRITE_UNKNOWN;
			if (SQL_ATTR_READONLY != value)
			{
				const char *name = fi ? SAFE_NAME(fi->column_name) : QR_get_fieldname(SC_get_Curres(stmt), col_idx);
				if (stricmp(name, OID_NAME) == 0 ||
				    stricmp(name, "ctid") == 0 ||
				    stricmp(name, "xmin") == 0)
					value = SQL_ATTR_READONLY;
			}

			mylog("%s: UPDATEABLE = %d\n", func, value);
			break;
#if (ODBCVER >= 0x0300)
		case SQL_DESC_BASE_COLUMN_NAME:

			p = fi ? SAFE_NAME(fi->column_name) : QR_get_fieldname(SC_get_Curres(stmt), col_idx);

			mylog("%s: BASE_COLUMN_NAME = '%s'\n", func, p);
			break;
		case SQL_DESC_BASE_TABLE_NAME: /* the same as TABLE_NAME ok ? */
			p = ti ? SAFE_NAME(ti->table_name) : NULL_STRING;

			mylog("%s: BASE_TABLE_NAME = '%s'\n", func, p);
			break;
		case SQL_DESC_LENGTH: /* different from SQL_COLUMN_LENGTH */
			value = (fi && fi->length > 0) ? fi->length : pgtype_desclength(stmt, field_type, col_idx, unknown_sizes);
			if (value < 0)
				value = 0;

			mylog("%s: col %d, length = %d\n", func, col_idx, value);
			break;
		case SQL_DESC_OCTET_LENGTH:
			value = (fi && fi->length > 0) ? fi->length : pgtype_transfer_octet_length(stmt, field_type, col_idx, unknown_sizes);
			if (value < 0)
				value = 0;
			mylog("%s: col %d, octet_length = %d\n", func, col_idx, value);
			break;
		case SQL_DESC_PRECISION: /* different from SQL_COLUMN_PRECISION */
			if (value = FI_precision(fi), value <= 0)
				value = pgtype_precision(stmt, field_type, col_idx, unknown_sizes);
			if (value < 0)
				value = 0;

			mylog("%s: col %d, desc_precision = %d\n", func, col_idx, value);
			break;
		case SQL_DESC_SCALE: /* different from SQL_COLUMN_SCALE */
			value = pgtype_scale(stmt, field_type, col_idx);
			if (value < 0)
				value = 0;
			break;
		case SQL_DESC_LOCAL_TYPE_NAME:
			p = pgtype_to_name(stmt, field_type);
			break;
		case SQL_DESC_TYPE:
			value = pgtype_to_sqldesctype(stmt, field_type, col_idx);
			break;
		case SQL_DESC_NUM_PREC_RADIX:
			value = pgtype_radix(stmt, field_type);
			break;
		case SQL_DESC_LITERAL_PREFIX:
			p = pgtype_literal_prefix(stmt, field_type);
			break;
		case SQL_DESC_LITERAL_SUFFIX:
			p = pgtype_literal_suffix(stmt, field_type);
			break;
		case SQL_DESC_UNNAMED:
			value = (fi && NAME_IS_NULL(fi->column_name) && NAME_IS_NULL(fi->column_alias)) ? SQL_UNNAMED : SQL_NAMED;
			break;
#endif /* ODBCVER */
		case 1212: /* SQL_CA_SS_COLUMN_KEY ? */
			SC_set_error(stmt, STMT_OPTION_NOT_FOR_THE_DRIVER, "this request may be for MS SQL Server", func);
			return SQL_ERROR;
		default:
			SC_set_error(stmt, STMT_INVALID_OPTION_IDENTIFIER, "ColAttribute for this type not implemented yet", func);
			return SQL_ERROR;
	}

	result = SQL_SUCCESS;

	if (p)
	{							/* char/binary data */
		len = strlen(p);

		if (rgbDesc)
		{
			strncpy_null((char *) rgbDesc, p, (size_t) cbDescMax);

			if (len >= cbDescMax)
			{
				result = SQL_SUCCESS_WITH_INFO;
				SC_set_error(stmt, STMT_TRUNCATED, "The buffer was too small for the rgbDesc.", func);
			}
		}

		if (pcbDesc)
			*pcbDesc = len;
	}
	else
	{
		/* numeric data */
		if (pfDesc)
			*pfDesc = value;
	}

	return result;
}


/*	Returns result data for a single column in the current row. */
RETCODE		SQL_API
PGAPI_GetData(
			  HSTMT hstmt,
			  SQLUSMALLINT icol,
			  SQLSMALLINT fCType,
			  PTR rgbValue,
			  SQLLEN cbValueMax,
			  SQLLEN FAR * pcbValue)
{
	CSTR func = "PGAPI_GetData";
	QResultClass *res;
	StatementClass *stmt = (StatementClass *) hstmt;
	int			num_cols,
				num_rows;
	Int4		field_type;
	void	   *value = NULL;
	RETCODE		result = SQL_SUCCESS;
	char		get_bookmark = FALSE;
	ConnInfo   *ci;

	mylog("%s: enter, stmt=%x\n", func, stmt);

	if (!stmt)
	{
		SC_log_error(func, "", NULL);
		return SQL_INVALID_HANDLE;
	}
	ci = &(SC_get_conn(stmt)->connInfo);
	res = SC_get_Curres(stmt);

	if (STMT_EXECUTING == stmt->status)
	{
		SC_set_error(stmt, STMT_SEQUENCE_ERROR, "Can't get data while statement is still executing.", func);
		return SQL_ERROR;
	}

	if (stmt->status != STMT_FINISHED)
	{
		SC_set_error(stmt, STMT_STATUS_ERROR, "GetData can only be called after the successful execution on a SQL statement", func);
		return SQL_ERROR;
	}

	if (icol == 0)
	{
		if (stmt->options.use_bookmarks == SQL_UB_OFF)
		{
			SC_set_error(stmt, STMT_COLNUM_ERROR, "Attempt to retrieve bookmark with bookmark usage disabled", func);
			return SQL_ERROR;
		}

		/* Make sure it is the bookmark data type */
		switch (fCType)
		{
			case SQL_C_BOOKMARK:
#if (ODBCVER >= 0x0300)
			case SQL_C_VARBOOKMARK:
#endif /* ODBCVER */
				break;
			default:
inolog("GetData Column 0 is type %d not of type SQL_C_BOOKMARK", fCType);
				SC_set_error(stmt, STMT_PROGRAM_TYPE_OUT_OF_RANGE, "Column 0 is not of type SQL_C_BOOKMARK", func);
				return SQL_ERROR;
		}

		get_bookmark = TRUE;
	}
	else
	{
		/* use zero-based column numbers */
		icol--;

		/* make sure the column number is valid */
		num_cols = QR_NumPublicResultCols(res);
		if (icol >= num_cols)
		{
			SC_set_error(stmt, STMT_INVALID_COLUMN_NUMBER_ERROR, "Invalid column number.", func);
			return SQL_ERROR;
		}
	}

#define	return	DONT_CALL_RETURN_FROM_HERE???
	/* StartRollbackState(stmt); */
	if (!SC_is_fetchcursor(stmt))
	{
		/* make sure we're positioned on a valid row */
		num_rows = QR_get_num_total_tuples(res);
		if ((stmt->currTuple < 0) ||
			(stmt->currTuple >= num_rows))
		{
			SC_set_error(stmt, STMT_INVALID_CURSOR_STATE_ERROR, "Not positioned on a valid row for GetData.", func);
			result = SQL_ERROR;
			goto cleanup;
		}
		mylog("     num_rows = %d\n", num_rows);

		if (!get_bookmark)
		{
			Int4	curt = GIdx2CacheIdx(stmt->currTuple, stmt, res);
			value = QR_get_value_backend_row(res, curt, icol);
inolog("currT=%d base=%d rowset=%d\n", stmt->currTuple, QR_get_rowstart_in_cache(res), SC_get_rowset_start(stmt)); 
			mylog("     value = '%s'\n", value ? value : "(null)");
		}
	}
	else
	{
		/* it's a SOCKET result (backend data) */
		if (stmt->currTuple == -1 || !res || !res->tupleField)
		{
			SC_set_error(stmt, STMT_INVALID_CURSOR_STATE_ERROR, "Not positioned on a valid row for GetData.", func);
			result = SQL_ERROR;
			goto cleanup;
		}

		if (!get_bookmark)
		{
			/** value = QR_get_value_backend(res, icol); maybe thiw doesn't work */
			Int4	curt = GIdx2CacheIdx(stmt->currTuple, stmt, res);
			value = QR_get_value_backend_row(res, curt, icol);
		}
		mylog("  socket: value = '%s'\n", value ? value : "(null)");
	}

	if (get_bookmark)
	{
		BOOL	contents_get = FALSE;

		if (rgbValue)
		{
			if (SQL_C_BOOKMARK == fCType || 4 <= cbValueMax)
			{
				contents_get = TRUE; 
				*((SQLULEN *) rgbValue) = SC_get_bookmark(stmt);
			}
		}
		if (pcbValue)
			*pcbValue = sizeof(SQLULEN);

		if (contents_get)
			result = SQL_SUCCESS;
		else
		{
			SC_set_error(stmt, STMT_TRUNCATED, "The buffer was too small for the GetData.", func);
			result = SQL_SUCCESS_WITH_INFO;
		}
		goto cleanup;
	}

	field_type = QR_get_field_type(res, icol);

	mylog("**** %s: icol = %d, fCType = %d, field_type = %d, value = '%s'\n", func, icol, fCType, field_type, value ? value : "(null)");

	SC_set_current_col(stmt, icol);

	result = copy_and_convert_field(stmt, field_type, value,
								 fCType, rgbValue, cbValueMax, pcbValue);

	switch (result)
	{
		case COPY_OK:
			result = SQL_SUCCESS;
			break;

		case COPY_UNSUPPORTED_TYPE:
			SC_set_error(stmt, STMT_RESTRICTED_DATA_TYPE_ERROR, "Received an unsupported type from Postgres.", func);
			result = SQL_ERROR;
			break;

		case COPY_UNSUPPORTED_CONVERSION:
			SC_set_error(stmt, STMT_RESTRICTED_DATA_TYPE_ERROR, "Couldn't handle the necessary data type conversion.", func);
			result = SQL_ERROR;
			break;

		case COPY_RESULT_TRUNCATED:
			SC_set_error(stmt, STMT_TRUNCATED, "The buffer was too small for the GetData.", func);
			result = SQL_SUCCESS_WITH_INFO;
			break;

		case COPY_GENERAL_ERROR:		/* error msg already filled in */
			result = SQL_ERROR;
			break;

		case COPY_NO_DATA_FOUND:
			/* SC_log_error(func, "no data found", stmt); */
			result = SQL_NO_DATA_FOUND;
			break;

		default:
			SC_set_error(stmt, STMT_INTERNAL_ERROR, "Unrecognized return value from copy_and_convert_field.", func);
			result = SQL_ERROR;
			break;
	}

cleanup:
#undef	return
	if (stmt->internal)
		result = DiscardStatementSvp(stmt, result, FALSE);
	return result;
}


/*
 *		Returns data for bound columns in the current row ("hstmt->iCursor"),
 *		advances the cursor.
 */
RETCODE		SQL_API
PGAPI_Fetch(
			HSTMT hstmt)
{
	CSTR func = "PGAPI_Fetch";
	StatementClass *stmt = (StatementClass *) hstmt;
	ARDFields	*opts;
	QResultClass *res;
	BindInfoClass	*bookmark;
	RETCODE		retval = SQL_SUCCESS;

	mylog("%s: stmt = %x, stmt->result= %x\n", func, stmt, stmt ? SC_get_Curres(stmt) : NULL);

	if (!stmt)
	{
		SC_log_error(func, "", NULL);
		return SQL_INVALID_HANDLE;
	}

	SC_clear_error(stmt);

	if (!(res = SC_get_Curres(stmt)))
	{
		SC_set_error(stmt, STMT_INVALID_CURSOR_STATE_ERROR, "Null statement result in PGAPI_Fetch.", func);
		return SQL_ERROR;
	}

	/* Not allowed to bind a bookmark column when using SQLFetch. */
	opts = SC_get_ARDF(stmt);
	if ((bookmark = opts->bookmark) && bookmark->buffer)
	{
		SC_set_error(stmt, STMT_COLNUM_ERROR, "Not allowed to bind a bookmark column when using PGAPI_Fetch", func);
		return SQL_ERROR;
	}

	if (stmt->status == STMT_EXECUTING)
	{
		SC_set_error(stmt, STMT_SEQUENCE_ERROR, "Can't fetch while statement is still executing.", func);
		return SQL_ERROR;
	}

	if (stmt->status != STMT_FINISHED)
	{
		SC_set_error(stmt, STMT_SEQUENCE_ERROR, "Fetch can only be called after the successful execution on a SQL statement", func);
		return SQL_ERROR;
	}

	if (opts->bindings == NULL)
	{
		if (stmt->statement_type != STMT_TYPE_SELECT)
			return SQL_NO_DATA_FOUND;
		/* just to avoid a crash if the user insists on calling this */
		/* function even if SQL_ExecDirect has reported an Error */
		SC_set_error(stmt, STMT_INVALID_CURSOR_STATE_ERROR, "Bindings were not allocated properly.", func);
		return SQL_ERROR;
	}

#define	return	DONT_CALL_RETURN_FROM_HERE???
	/* StartRollbackState(stmt); */
	if (stmt->rowset_start < 0)
		SC_set_rowset_start(stmt, 0, TRUE);
	QR_set_rowset_size(res, 1);
	/* QR_inc_rowstart_in_cache(res, stmt->last_fetch_count_include_ommitted); */
	SC_inc_rowset_start(stmt, stmt->last_fetch_count_include_ommitted);

	retval = SC_fetch(stmt);
#undef	return
	if (stmt->internal)
		retval = DiscardStatementSvp(stmt, retval, FALSE);
	return retval;
}

static RETCODE SQL_API
SC_pos_reload_needed(StatementClass *stmt, UInt4 req_size, UDWORD flag);
Int4
getNthValid(const QResultClass *res, Int4 sta, UWORD orientation, UInt4 nth, Int4 *nearest)
{
	Int4	i, num_tuples = QR_get_num_total_tuples(res), nearp;
	UInt4	count;
	KeySet	*keyset;

	if (!QR_once_reached_eof(res))
		num_tuples = INT_MAX;
	/* Note that the parameter nth is 1-based */
inolog("get %dth Valid data from %d to %s [dlt=%d]", nth, sta, orientation == SQL_FETCH_PRIOR ? "backward" : "forward", res->dl_count);
	if (0 == res->dl_count)
	{
		if (SQL_FETCH_PRIOR == orientation)
		{	
			if (sta + 1 >= (Int4) nth)
			{
				*nearest = sta + 1 - nth;
				return nth;
			}
			*nearest = -1;
			return -(Int4)(sta + 1);
		}
		else
		{	
			nearp = sta - 1 + nth;
			if (nearp < num_tuples)
			{
				*nearest = nearp;
				return nth;
			}
			*nearest = num_tuples;
			return -(Int4)(num_tuples - sta);
		}
	}
	count = 0;
	if (QR_get_cursor(res))
	{
		UInt4	*deleted = res->deleted;

		*nearest = sta - 1 + nth;
		if (SQL_FETCH_PRIOR == orientation)
		{
			for (i = res->dl_count - 1; i >=0 && *nearest <= (Int4) deleted[i]; i--)
			{
inolog("deleted[%d]=%d\n", i, deleted[i]);
				if (sta >= (Int4)deleted[i])
					(*nearest)--;
			}
inolog("nearest=%d\n", *nearest);
			if (*nearest < 0)
			{
				*nearest = -1;
				count = sta + 1;
			}
			else
				return nth;
		}
		else
		{
			if (!QR_once_reached_eof(res))
				num_tuples = INT_MAX;
			for (i = 0; i < res->dl_count && *nearest >= (Int4)deleted[i]; i++)
			{
				if (sta <= (Int4)deleted[i])
					(*nearest)++;
			}
			if (*nearest >= num_tuples)
			{
				*nearest = num_tuples;
				count = *nearest - sta;
			}
			else
				return nth;
		}
	}
	else if (SQL_FETCH_PRIOR == orientation)
	{
		for (i = sta, keyset = res->keyset + sta;
			i >= 0; i--, keyset--)
		{
			if (0 == (keyset->status & (CURS_SELF_DELETING | CURS_SELF_DELETED | CURS_OTHER_DELETED)))
			{
				*nearest = i;
inolog(" nearest=%d\n", *nearest);
				if (++count == nth)
					return count;
			}
		}
		*nearest = -1; 
	}
	else
	{
		for (i = sta, keyset = res->keyset + sta;
			i < num_tuples; i++, keyset++)
		{
			if (0 == (keyset->status & (CURS_SELF_DELETING | CURS_SELF_DELETED | CURS_OTHER_DELETED)))
			{
				*nearest = i;
inolog(" nearest=%d\n", *nearest);
				if (++count == nth)
					return count;
			}
		}
		*nearest = num_tuples; 
	}
inolog(" nearest not found\n");
	return -(Int4)count;
}

static void
move_cursor_position_if_needed(StatementClass *self, QResultClass *res)
{
	int	move_offset;
	
	/*
	 * The move direction must be initialized to is_not_moving or
	 * is_moving_from_the_last in advance.
	 */
	if (!QR_get_cursor(res))
	{
		QR_stop_movement(res); /* for safety */
		res->move_offset = 0;
		return;
	}
inolog("BASE=%d numb=%d curr=%d cursT=%d\n", QR_get_rowstart_in_cache(res), res->num_cached_rows, self->currTuple, res->cursTuple);

	/* retrieve "move from the last" case first */
	if (QR_is_moving_from_the_last(res))
	{
		mylog("must MOVE from the last\n");
		if (QR_once_reached_eof(res) || self->rowset_start <= QR_get_num_total_tuples(res)) /* this shouldn't happen */
			mylog("strange situation in move from the last\n");
		if (0 == res->move_offset)
			res->move_offset = INT_MAX - self->rowset_start;
else
{
inolog("!!move_offset=%d calc=%d\n", res->move_offset, INT_MAX - self->rowset_start);
}
		return;
	}

	/* normal case */
	res->move_offset = 0;
	move_offset = self->currTuple - res->cursTuple;
	if (QR_get_rowstart_in_cache(res) >= 0 &&
	     QR_get_rowstart_in_cache(res) <= (Int4)res->num_cached_rows)
	{
		QR_set_next_in_cache(res, (QR_get_rowstart_in_cache(res) < 0) ? 0 : QR_get_rowstart_in_cache(res));
		return;
	}
	if (0 == move_offset) 
		return;
	if (move_offset > 0)
	{
		QR_set_move_forward(res);
		res->move_offset = move_offset;
	}
	else
	{
		QR_set_move_backward(res);
		res->move_offset = -move_offset;
	}
}
/*
 *	return NO_DATA_FOUND macros
 *	  save_rowset_start or num_tuples must be defined 
 */
#define	EXTFETCH_RETURN_BOF(stmt, res) \
{ \
inolog("RETURN_BOF\n"); \
	SC_set_rowset_start(stmt, -1, TRUE); \
	stmt->currTuple = -1; \
	/* move_cursor_position_if_needed(stmt, res); */ \
	return SQL_NO_DATA_FOUND; \
}
#define	EXTFETCH_RETURN_EOF(stmt, res) \
{ \
inolog("RETURN_EOF\n"); \
	SC_set_rowset_start(stmt, num_tuples, TRUE); \
	stmt->currTuple = -1; \
	/* move_cursor_position_if_needed(stmt, res); */ \
	return SQL_NO_DATA_FOUND; \
}
	
/*	This fetchs a block of data (rowset). */
RETCODE		SQL_API
PGAPI_ExtendedFetch(
					HSTMT hstmt,
					SQLUSMALLINT fFetchType,
					SQLLEN irow,
					SQLULEN FAR * pcrow,
					SQLUSMALLINT FAR * rgfRowStatus,
					SQLINTEGER bookmark_offset,
					SQLINTEGER rowsetSize)
{
	CSTR func = "PGAPI_ExtendedFetch";
	StatementClass *stmt = (StatementClass *) hstmt;
	ARDFields	*opts;
	QResultClass *res;
	BindInfoClass	*bookmark;
	int			num_tuples,
				i, fc_io;
	Int4			save_rowset_size,
				save_rowset_start,
				progress_size,
				rowset_start;
	RETCODE		result = SQL_SUCCESS;
	char		truncated, error, should_set_rowset_start = FALSE; 
	ConnInfo   *ci;
	Int4		currp;
	UWORD		pstatus;
	BOOL		currp_is_valid, reached_eof;

	mylog("%s: stmt=%x rowsetSize=%d\n", func, stmt, rowsetSize);

	if (!stmt)
	{
		SC_log_error(func, "", NULL);
		return SQL_INVALID_HANDLE;
	}
	ci = &(SC_get_conn(stmt)->connInfo);

	/* if (SC_is_fetchcursor(stmt) && !stmt->manual_result) */
	if (SQL_CURSOR_FORWARD_ONLY == stmt->options.cursor_type)
	{
		if (fFetchType != SQL_FETCH_NEXT)
		{
			SC_set_error(stmt, STMT_FETCH_OUT_OF_RANGE, "The fetch type for PGAPI_ExtendedFetch isn't allowed with ForwardOnly cursor.", func);
			return SQL_ERROR;
		}
	}

	SC_clear_error(stmt);

	if (!(res = SC_get_Curres(stmt)))
	{
		SC_set_error(stmt, STMT_INVALID_CURSOR_STATE_ERROR, "Null statement result in PGAPI_ExtendedFetch.", func);
		return SQL_ERROR;
	}

	opts = SC_get_ARDF(stmt);
	/*
	 * If a bookmark colunmn is bound but bookmark usage is off, then
	 * error
	 */
	if ((bookmark = opts->bookmark) && bookmark->buffer && stmt->options.use_bookmarks == SQL_UB_OFF)
	{
		SC_set_error(stmt, STMT_COLNUM_ERROR, "Attempt to retrieve bookmark with bookmark usage disabled", func);
		return SQL_ERROR;
	}

	if (stmt->status == STMT_EXECUTING)
	{
		SC_set_error(stmt, STMT_SEQUENCE_ERROR, "Can't fetch while statement is still executing.", func);
		return SQL_ERROR;
	}

	if (stmt->status != STMT_FINISHED)
	{
		SC_set_error(stmt, STMT_STATUS_ERROR, "ExtendedFetch can only be called after the successful execution on a SQL statement", func);
		return SQL_ERROR;
	}

	if (opts->bindings == NULL)
	{
		if (stmt->statement_type != STMT_TYPE_SELECT)
			return SQL_NO_DATA_FOUND;
		/* just to avoid a crash if the user insists on calling this */
		/* function even if SQL_ExecDirect has reported an Error */
		SC_set_error(stmt, STMT_INVALID_CURSOR_STATE_ERROR, "Bindings were not allocated properly.", func);
		return SQL_ERROR;
	}

	/* Initialize to no rows fetched */
	if (rgfRowStatus)
		for (i = 0; i < rowsetSize; i++)
			*(rgfRowStatus + i) = SQL_ROW_NOROW;

	if (pcrow)
		*pcrow = 0;

	num_tuples = QR_get_num_total_tuples(res);
	reached_eof = QR_once_reached_eof(res) && QR_get_cursor(res);
	if (SC_is_fetchcursor(stmt) && !reached_eof)
		num_tuples = INT_MAX;

inolog("num_tuples=%d\n", num_tuples);
	/* Save and discard the saved rowset size */
	save_rowset_start = SC_get_rowset_start(stmt);
	save_rowset_size = stmt->save_rowset_size;
	stmt->save_rowset_size = -1;
	rowset_start = SC_get_rowset_start(stmt);

	QR_stop_movement(res);
	res->move_offset = 0;
	switch (fFetchType)
	{
		case SQL_FETCH_NEXT:

			/*
			 * From the odbc spec... If positioned before the start of the
			 * RESULT SET, then this should be equivalent to
			 * SQL_FETCH_FIRST.
			 */

			progress_size = (save_rowset_size > 0 ? save_rowset_size : rowsetSize);
			if (rowset_start < 0)
				SC_set_rowset_start(stmt, 0, TRUE);
			else if (res->keyset)
			{
				if (stmt->last_fetch_count <= progress_size)
				{
					SC_inc_rowset_start(stmt, stmt->last_fetch_count_include_ommitted);
					progress_size -= stmt->last_fetch_count;
				}
				if (progress_size > 0)
				{
					if (getNthValid(res, SC_get_rowset_start(stmt),
						SQL_FETCH_NEXT, progress_size + 1,
						&rowset_start) <= 0)
					{
						EXTFETCH_RETURN_EOF(stmt, res)
					}
					else
						should_set_rowset_start =TRUE;
				}
			}
			else
				SC_inc_rowset_start(stmt, progress_size);
			mylog("SQL_FETCH_NEXT: num_tuples=%d, currtuple=%d, rowst=%d\n", num_tuples, stmt->currTuple, rowset_start);
			break;

		case SQL_FETCH_PRIOR:
			mylog("SQL_FETCH_PRIOR: num_tuples=%d, currtuple=%d\n", num_tuples, stmt->currTuple);

			/*
			 * From the odbc spec... If positioned after the end of the
			 * RESULT SET, then this should be equivalent to
			 * SQL_FETCH_LAST.
			 */
			if (SC_get_rowset_start(stmt) <= 0)
			{
				EXTFETCH_RETURN_BOF(stmt, res)
			}
			if (SC_get_rowset_start(stmt) >= num_tuples)
			{
				if (rowsetSize > num_tuples)
				{
					SC_set_error(stmt, STMT_POS_BEFORE_RECORDSET, "fetch prior from eof and before the beginning", func);
				}
				SC_set_rowset_start(stmt, num_tuples <= 0 ? 0 : (num_tuples - rowsetSize), TRUE);
			}
			else if (QR_haskeyset(res))
			{
				if (i = getNthValid(res, SC_get_rowset_start(stmt) - 1, SQL_FETCH_PRIOR, rowsetSize, &rowset_start), i < -1)
				{
					SC_set_error(stmt, STMT_POS_BEFORE_RECORDSET, "fetch prior and before the beggining", func);
					SC_set_rowset_start(stmt, 0, TRUE);
				}
				else if (i <= 0)
				{
					EXTFETCH_RETURN_BOF(stmt, res)
				}
				else
					should_set_rowset_start = TRUE;
			}
			else if (SC_get_rowset_start(stmt) < rowsetSize)
			{
				SC_set_error(stmt, STMT_POS_BEFORE_RECORDSET, "fetch prior from eof and before the beggining", func);
				SC_set_rowset_start(stmt, 0, TRUE);
			}
			else
				SC_inc_rowset_start(stmt, -rowsetSize);
			break;

		case SQL_FETCH_FIRST:
			mylog("SQL_FETCH_FIRST: num_tuples=%d, currtuple=%d\n", num_tuples, stmt->currTuple);

			SC_set_rowset_start(stmt, 0, TRUE);
			break;

		case SQL_FETCH_LAST:
			mylog("SQL_FETCH_LAST: num_tuples=%d, currtuple=%d\n", num_tuples, stmt->currTuple);

			if (!reached_eof)
			{
				QR_set_move_from_the_last(res);
				res->move_offset = rowsetSize;
			}
			SC_set_rowset_start(stmt, num_tuples <= 0 ? 0 : (num_tuples - rowsetSize), TRUE);
			break;

		case SQL_FETCH_ABSOLUTE:
			mylog("SQL_FETCH_ABSOLUTE: num_tuples=%d, currtuple=%d, irow=%d\n", num_tuples, stmt->currTuple, irow);

			/* Position before result set, but dont fetch anything */
			if (irow == 0)
			{
				EXTFETCH_RETURN_BOF(stmt, res)
			}
			/* Position before the desired row */
			else if (irow > 0)
			{
				if (getNthValid(res, 0, SQL_FETCH_NEXT, irow, &rowset_start) <= 0)
				{
					EXTFETCH_RETURN_EOF(stmt, res)
				}
				else
					should_set_rowset_start = TRUE;
			}
			/* Position with respect to the end of the result set */
			else
			{
				if (getNthValid(res, num_tuples - 1, SQL_FETCH_PRIOR, -irow, &rowset_start) <= 0)
				{
					EXTFETCH_RETURN_BOF(stmt, res)
				}
				else
				{
					if (!reached_eof)
					{
						QR_set_move_from_the_last(res);
						res->move_offset = -irow;
					}
					should_set_rowset_start = TRUE;
				}
			}
			break;

		case SQL_FETCH_RELATIVE:

			/*
			 * Refresh the current rowset -- not currently implemented,
			 * but lie anyway
			 */
			if (irow == 0)
				break;

			if (irow > 0)
			{
				if (getNthValid(res, SC_get_rowset_start(stmt) + 1, SQL_FETCH_NEXT, irow, &rowset_start) <= 0)
				{
					EXTFETCH_RETURN_EOF(stmt, res)
				}
				else
					should_set_rowset_start = TRUE;
			}
			else
			{
				if (getNthValid(res, SC_get_rowset_start(stmt) - 1, SQL_FETCH_PRIOR, -irow, &rowset_start) <= 0)
				{
					EXTFETCH_RETURN_BOF(stmt, res)
				}
				else
					should_set_rowset_start = TRUE;
			}
			break;

		case SQL_FETCH_BOOKMARK:
			{
			Int4	bidx = SC_resolve_bookmark(irow);

			if (bidx < 0)
			{
				if (!reached_eof)
				{
					QR_set_move_from_the_last(res);
					res->move_offset = 1 + res->ad_count + bidx;
				}
				bidx = num_tuples - 1 - res->ad_count - bidx;
			} 

			rowset_start = bidx;
			if (bookmark_offset >= 0)
			{
				if (getNthValid(res, bidx, SQL_FETCH_NEXT, bookmark_offset + 1, &rowset_start) <= 0)
				{
					EXTFETCH_RETURN_EOF(stmt, res)
				}
				else
					should_set_rowset_start = TRUE;
			}
			else if (getNthValid(res, bidx, SQL_FETCH_PRIOR, 1 - bookmark_offset, &rowset_start) <= 0)
			{
				stmt->currTuple = -1;
				EXTFETCH_RETURN_BOF(stmt, res)
			}
			else
				should_set_rowset_start = TRUE;
			}
			break;

		default:
			SC_set_error(stmt, STMT_FETCH_OUT_OF_RANGE, "Unsupported PGAPI_ExtendedFetch Direction", func);
			return SQL_ERROR;
	}

	/*
	 * CHECK FOR PROPER CURSOR STATE
	 */

	/*
	 * Handle Declare Fetch style specially because the end is not really
	 * the end...
	 */
	if (!should_set_rowset_start)
		rowset_start = SC_get_rowset_start(stmt);
	if (SC_is_fetchcursor(stmt))
	{
		if (reached_eof &&
		    rowset_start >= num_tuples)
		{
			EXTFETCH_RETURN_EOF(stmt, res)
		}
	}
	else
	{
		/* If *new* rowset is after the result_set, return no data found */
		if (rowset_start >= num_tuples)
		{
			EXTFETCH_RETURN_EOF(stmt, res)
		}
	}
	/* If *new* rowset is prior to result_set, return no data found */
	if (rowset_start < 0)
	{
		if (rowset_start + rowsetSize <= 0)
		{
			EXTFETCH_RETURN_BOF(stmt, res)
		}
		else
		{	/* overlap with beginning of result set,
			 * so get first rowset */
			SC_set_rowset_start(stmt, 0, TRUE);
		}
		should_set_rowset_start = FALSE;
	}

#define	return DONT_CALL_RETURN_FROM_HERE???
	/* increment the base row in the tuple cache */
	QR_set_rowset_size(res, rowsetSize);
	/* set the rowset_start if needed */
	if (should_set_rowset_start)
		SC_set_rowset_start(stmt, rowset_start, TRUE);
	/* currTuple is always 1 row prior to the rowset start */
	stmt->currTuple = RowIdx2GIdx(-1, stmt);

	if (SC_is_fetchcursor(stmt) ||
	    SQL_CURSOR_KEYSET_DRIVEN == stmt->options.cursor_type)
	{
		move_cursor_position_if_needed(stmt, res);
	}
	else
		QR_set_rowstart_in_cache(res, SC_get_rowset_start(stmt));

	if (res->keyset && !QR_get_cursor(res))
	{
		UDWORD	flag = 0;
		Int4	rowset_end, req_size;

		getNthValid(res, rowset_start, SQL_FETCH_NEXT, rowsetSize, &rowset_end);
		req_size = rowset_end - rowset_start + 1;
		if (SQL_CURSOR_KEYSET_DRIVEN == stmt->options.cursor_type)
		{
			if (fFetchType != SQL_FETCH_NEXT ||
		    		QR_get_rowstart_in_cache(res) + req_size > (Int4)QR_get_num_cached_tuples(res))
				flag = 1;
		}
		if (SQL_RD_ON == stmt->options.retrieve_data ||
		    flag != 0)
		{
			SC_pos_reload_needed(stmt, req_size, flag);
		}
	}
	/* Physical Row advancement occurs for each row fetched below */

	mylog("PGAPI_ExtendedFetch: new currTuple = %d\n", stmt->currTuple);

	truncated = error = FALSE;

	currp = -1;
	stmt->bind_row = 0;		/* set the binding location */
	result = SC_fetch(stmt);
	if (SQL_NO_DATA_FOUND != result && res->keyset)
	{
		currp = GIdx2KResIdx(SC_get_rowset_start(stmt), stmt, res);
inolog("currp=%d\n", currp);
		if (currp < 0)
		{
			result = SQL_ERROR;
			mylog("rowset_start=%d but currp=%d\n", SC_get_rowset_start(stmt), currp);
			SC_set_error(stmt, STMT_INTERNAL_ERROR, "rowset_start not in the keyset", func);
			goto cleanup;
		}
	}
	for (i = 0, fc_io = 0; SQL_NO_DATA_FOUND != result; currp++)
	{
		fc_io++;
		currp_is_valid = FALSE;
		if (res->keyset)
		{
			if (currp < (Int4)res->num_cached_keys)
			{
				currp_is_valid = TRUE;
				res->keyset[currp].status &= ~CURS_IN_ROWSET; /* Off the flag first */
			}
			else
			{
				mylog("Umm current row is out of keyset\n");
				break;
			}
		}
inolog("ExtFetch result=%d\n", result);
		if (currp_is_valid && SQL_SUCCESS_WITH_INFO == result && 0 == stmt->last_fetch_count)
		{
inolog("just skipping deleted row %d\n", currp);
			QR_set_rowset_size(res, rowsetSize - i + fc_io);
			result = SC_fetch(stmt);
			continue;
		}

		/* Determine Function status */
		if (result == SQL_SUCCESS_WITH_INFO)
			truncated = TRUE;
		else if (result == SQL_ERROR)
			error = TRUE;

		/* Determine Row Status */
		if (rgfRowStatus)
		{
			if (result == SQL_ERROR)
				*(rgfRowStatus + i) = SQL_ROW_ERROR;
			else if (currp_is_valid)
			{
				pstatus = (res->keyset[currp].status & KEYSET_INFO_PUBLIC);
				if (pstatus != 0 && pstatus != SQL_ROW_ADDED)
				{
					rgfRowStatus[i] = pstatus;
				}
				else
					rgfRowStatus[i] = SQL_ROW_SUCCESS;
				/* refresh the status */
				/* if (SQL_ROW_DELETED != pstatus) */
				res->keyset[currp].status &= (~KEYSET_INFO_PUBLIC);
			}
			else
				*(rgfRowStatus + i) = SQL_ROW_SUCCESS;
		}
		if (SQL_ERROR != result && currp_is_valid)
			res->keyset[currp].status |= CURS_IN_ROWSET; /* This is the unique place where the CURS_IN_ROWSET bit is turned on */
		i++;
		if (i >= rowsetSize)
			break;
		stmt->bind_row = i;		/* set the binding location */
		result = SC_fetch(stmt);
	}

	/* Save the fetch count for SQLSetPos */
	stmt->last_fetch_count = i;
	/*
	currp = KResIdx2GIdx(currp, stmt, res);
	stmt->last_fetch_count_include_ommitted = GIdx2RowIdx(currp, stmt);
	*/
	stmt->last_fetch_count_include_ommitted = fc_io;

	/* Reset next binding row */
	stmt->bind_row = 0;

	/* Move the cursor position to the first row in the result set. */
	stmt->currTuple = RowIdx2GIdx(0, stmt);

	/* For declare/fetch, need to reset cursor to beginning of rowset */
	if (SC_is_fetchcursor(stmt))
		QR_set_position(res, 0);

	/* Set the number of rows retrieved */
	if (pcrow)
		*pcrow = i;
inolog("pcrow=%d\n", i);

	if (i == 0)
		/* Only DeclareFetch should wind up here */
		result = SQL_NO_DATA_FOUND;
	else if (error)
		result = SQL_ERROR;
	else if (truncated)
		result = SQL_SUCCESS_WITH_INFO;
	else if (SC_get_errornumber(stmt) == STMT_POS_BEFORE_RECORDSET)
		result = SQL_SUCCESS_WITH_INFO;
	else
		result = SQL_SUCCESS;

cleanup:
#undef	return
	if (stmt->internal)
		result = DiscardStatementSvp(stmt, result, FALSE);
	return result;
}


/*
 *		This determines whether there are more results sets available for
 *		the "hstmt".
 */
/* CC: return SQL_NO_DATA_FOUND since we do not support multiple result sets */
RETCODE		SQL_API
PGAPI_MoreResults(
				  HSTMT hstmt)
{
	CSTR func = "PGAPI_MoreResults";
	StatementClass	*stmt = (StatementClass *) hstmt;
	QResultClass	*res;
	RETCODE		ret = SQL_SUCCESS;

	mylog("%s: entering...\n", func);
	if (stmt && (res = SC_get_Curres(stmt)))
		SC_set_Curres(stmt, res->next);
	if (res = SC_get_Curres(stmt), res)
	{
		stmt->diag_row_count = res->recent_processed_row_count;
		SC_set_rowset_start(stmt, -1, FALSE);
		stmt->currTuple = -1;
	}
	else
		ret = SQL_NO_DATA_FOUND;
	mylog("%s: returning %d\n", func, ret);
	return ret;
}


/*
 *	Stuff for updatable cursors.
 */
static Int2	getNumResultCols(const QResultClass *res)
{
	Int2	res_cols = QR_NumPublicResultCols(res);
	return res_cols;
}
static UInt4	getOid(const QResultClass *res, int index)
{
	return res->keyset[index].oid;
}
static void getTid(const QResultClass *res, int index, UInt4 *blocknum, UInt2 *offset)
{
	*blocknum = res->keyset[index].blocknum;
	*offset = res->keyset[index].offset;
}
static void KeySetSet(const TupleField *tuple, int num_fields, int num_key_fields, KeySet *keyset)
{
	sscanf(tuple[num_fields - num_key_fields].value, "(%u,%hu)",
			&keyset->blocknum, &keyset->offset);
	if (num_key_fields > 1)
		sscanf(tuple[num_fields - 1].value, "%u", &keyset->oid);
	else
		keyset->oid = 0;
}

static void AddRollback(StatementClass *stmt, QResultClass *res, int index, const KeySet *keyset, Int4 dmlcode)
{
	ConnectionClass	*conn = SC_get_conn(stmt);
	Rollback *rollback;

	if (!CC_is_in_trans(conn))
		return;
inolog("AddRollback %d(%d,%d) %s\n", index, keyset->blocknum, keyset->offset, dmlcode == SQL_ADD ? "ADD" : (dmlcode == SQL_UPDATE ? "UPDATE" : (dmlcode == SQL_DELETE ? "DELETE" : "REFRESH")));
	if (!res->rollback)
	{
		res->rb_count = 0;
		res->rb_alloc = 10;
		rollback = res->rollback = malloc(sizeof(Rollback) * res->rb_alloc);
	}
	else
	{
		if (res->rb_count >= res->rb_alloc)
		{
			res->rb_alloc *= 2; 
			if (rollback = realloc(res->rollback, sizeof(Rollback) * res->rb_alloc), !rollback)
			{
				res->rb_alloc = res->rb_count = 0;
				return;
			}
			res->rollback = rollback; 
		}
		rollback = res->rollback + res->rb_count;
	}
	rollback->index = index;
	rollback->option = dmlcode;
	rollback->offset = 0;
	rollback->blocknum = 0;
	if (keyset)
	{
		rollback->blocknum = keyset->blocknum;
		rollback->offset = keyset->offset;
	}

	conn->result_uncommitted = 1;
	res->rb_count++;	
}

int ClearCachedRows(TupleField *tuple, int num_fields, int num_rows)
{
	int	i;

	for (i = 0; i < num_fields * num_rows; i++, tuple++)
	{
		if (tuple->value)
		{
inolog("freeing tuple[%d][%d].value=%x\n", i / num_fields, i % num_fields, tuple->value);
			free(tuple->value);
			tuple->value = NULL;
		}
		tuple->len = -1;
	}
	return i;
}
int ReplaceCachedRows(TupleField *otuple, const TupleField *ituple, int num_fields, int num_rows)
{
	int	i;

inolog("ReplaceCachedRows %x num_fields=%d num_rows=%d\n", otuple, num_fields, num_rows);
	for (i = 0; i < num_fields * num_rows; i++, ituple++, otuple++)
	{
		if (otuple->value)
		{
			free(otuple->value);
			otuple->value = NULL;
		}
		if (ituple->value)
{
			otuple->value = strdup(ituple->value);
inolog("[%d,%d] %s copied\n", i / num_fields, i % num_fields, otuple->value);
}
		otuple->len = ituple->len;
	}
	return i;
}

int MoveCachedRows(TupleField *otuple, TupleField *ituple, Int4 num_fields, int num_rows)
{
	int	i;

inolog("MoveCachedRows %x num_fields=%d num_rows=%d\n", otuple, num_fields, num_rows);
	for (i = 0; i < num_fields * num_rows; i++, ituple++, otuple++)
	{
		if (otuple->value)
		{
			free(otuple->value);
			otuple->value = NULL;
		}
		if (ituple->value)
		{
			otuple->value = ituple->value;
			ituple->value = NULL;
inolog("[%d,%d] %s copied\n", i / num_fields, i % num_fields, otuple->value);
		}
		otuple->len = ituple->len;
		ituple->len = -1;
	}
	return i;
}

static BOOL	tupleExists(const StatementClass *stmt, const KeySet *keyset)
{
	char	selstr[256];
	const TABLE_INFO	*ti = stmt->ti[0];
	QResultClass	*res;
	RETCODE		ret = FALSE;

	if (NAME_IS_VALID(ti->schema_name))
		snprintf(selstr, sizeof(selstr), "select 1 from \"%s\".\"%s\" where ctid = '(%d,%d)'",
			SAFE_NAME(ti->schema_name), SAFE_NAME(ti->table_name), keyset->blocknum, keyset->offset);
	else
		snprintf(selstr, sizeof(selstr), "select 1 from \"%s\" where ctid = '(%d,%d)'",
			SAFE_NAME(ti->table_name), keyset->blocknum, keyset->offset);
	res = CC_send_query(SC_get_conn(stmt), selstr, NULL, 0, NULL);
	if (QR_command_maybe_successful(res) && 1 == res->num_cached_rows)
		ret = TRUE;
	QR_Destructor(res);
	return ret;
}
static BOOL	tupleIsAdding(const StatementClass *stmt, const QResultClass *res, Int4 index)
{
	int	i;
	BOOL	ret = FALSE;
	UWORD	status;

	if (!res->added_keyset)
		return ret;
	if (index < (Int4)res->num_total_read || index >= (Int4)QR_get_num_total_read(res))
		return ret;
	i = index - res->num_total_read; 
	status = res->added_keyset[i].status;
	if (0 == (status & CURS_SELF_ADDING))
		return ret;
	if (tupleExists(stmt, res->added_keyset + i))
		ret = TRUE;

	return ret;
}

static BOOL	tupleIsUpdating(const StatementClass *stmt, const QResultClass *res, Int4 index)
{
	int	i;
	BOOL	ret = FALSE;
	UWORD	status;

	if (!res->updated || !res->updated_keyset)
		return ret;
	for (i = res->up_count - 1; i >= 0; i--)
	{
		if (index == res->updated[i])
		{
			status = res->updated_keyset[i].status;
			if (0 == (status & CURS_SELF_UPDATING))
				continue;
			if (tupleExists(stmt, res->updated_keyset + i))
			{
				ret = TRUE;
				break;
			}
		} 
	}
	return ret;
}
static BOOL	tupleIsDeleting(const StatementClass *stmt, const QResultClass *res, Int4 index)
{
	int	i;
	BOOL	ret = FALSE;
	UWORD	status;

	if (!res->deleted || !res->deleted_keyset)
		return ret;
	for (i = 0; i < res->dl_count; i++)
	{
		if (index == res->deleted[i])
		{
			status = res->deleted_keyset[i].status;
			if (0 == (status & CURS_SELF_DELETING))
				;
			else if (tupleExists(stmt, res->deleted_keyset + i))
				;
			else
				ret = TRUE;
			break;
		} 
	}
	return ret;
}


static BOOL enlargeAdded(QResultClass *res, UInt4 number, const StatementClass *stmt)
{
	UInt4	alloc;
	KeySet	*added_keyset;
	TupleField	*added_tuples;
	int	num_fields = res->num_fields;

	alloc = res->ad_alloc;
	if (0 == alloc)
		alloc = number > 10 ? number : 10;
	else
		while (alloc < number)
		{
			alloc *= 2;
		}
 
	if (alloc <= res->ad_alloc)
		return TRUE;
	if (added_keyset = realloc(res->added_keyset, sizeof(KeySet) * alloc), !added_keyset)
	{
		res->ad_alloc = 0;
		return FALSE;
	}
	added_tuples = res->added_tuples;
	if (SQL_CURSOR_KEYSET_DRIVEN != stmt->options.cursor_type)
		if (added_tuples = realloc(res->added_tuples, sizeof(TupleField) * num_fields * alloc), !added_tuples)
		{
			if (added_keyset)
				free(added_keyset);
			added_keyset = NULL;
		}
	res->added_keyset = added_keyset; 
	res->added_tuples = added_tuples;
	if (!added_keyset)
	{
		res->ad_alloc = 0;
		return FALSE;
	}
	res->ad_alloc = alloc;
	return TRUE;
}
static void AddAdded(StatementClass *stmt, QResultClass *res, int index, const TupleField *tuple_added)
{
	KeySet	*added_keyset, *keyset, keys;
	TupleField	*added_tuples = NULL, *tuple;
	UInt4	ad_count;
	int	num_fields;

	if (!res)	return;
	num_fields = res->num_fields;
inolog("AddAdded index=%d, tuple=%x, num_fields=%d\n", index, tuple_added, num_fields);
	ad_count = res->ad_count;
	res->ad_count++;
	if (QR_get_cursor(res))
		index = -(Int4)res->ad_count;
	if (!tuple_added)
		return;
	KeySetSet(tuple_added, num_fields + res->num_key_fields, res->num_key_fields, &keys);
	keys.status = SQL_ROW_ADDED;
	if (CC_is_in_trans(SC_get_conn(stmt)))
		keys.status |= CURS_SELF_ADDING;
	else
		keys.status |= CURS_SELF_ADDED;
	AddRollback(stmt, res, index, &keys, SQL_ADD);

	if (!QR_get_cursor(res))
		return;
	if (ad_count > 0 && 0 == res->ad_alloc)
		return;
	if (!enlargeAdded(res, ad_count + 1, stmt))
		return;
	added_keyset = res->added_keyset; 
	added_tuples = res->added_tuples;

	keyset = added_keyset + ad_count;
	*keyset = keys; 
	if (added_tuples)
	{
		tuple = added_tuples + num_fields * ad_count;
		memset(tuple, 0, sizeof(TupleField) * num_fields);
		ReplaceCachedRows(tuple, tuple_added, num_fields, 1);
	}
}

static	void RemoveAdded(QResultClass *, Int4);
static	void RemoveUpdated(QResultClass *, Int4);
static	void RemoveUpdatedAfterTheKey(QResultClass *, Int4, const KeySet*);
static	void RemoveDeleted(QResultClass *, Int4);
static	void RemoveAdded(QResultClass *res, Int4 index)
{
	Int4	rmidx, num_fields = res->num_fields, mv_count;
	KeySet	*added_keyset;
	TupleField	*added_tuples;

	mylog("RemoveAdded index=%d\n", index);
	if (index < 0)
		rmidx = -index - 1;
	else
		rmidx = index - res->num_total_read;
	if (rmidx >= (Int4)res->ad_count)
		return;
	added_keyset = res->added_keyset + rmidx;
	added_tuples = res->added_tuples + num_fields * rmidx;
	ClearCachedRows(added_tuples, num_fields, 1);
	mv_count = res->ad_count - rmidx - 1;
	if (mv_count > 0)
	{
		memmove(added_keyset, added_keyset + 1, mv_count * sizeof(KeySet));
		memmove(added_tuples, added_tuples + num_fields, mv_count * num_fields * sizeof(TupleField));
	}
	RemoveDeleted(res, index);
	RemoveUpdated(res, index);
	res->ad_count--;
	mylog("RemoveAdded removed=1 count=%d\n", res->ad_count);
}

static void CommitAdded(QResultClass *res)
{
	KeySet	*added_keyset;
	int	i;
	UWORD	status;

	mylog("CommitAdded res=%x\n", res);
	if (!res || !res->added_keyset)	return;
	added_keyset = res->added_keyset;
	for (i = res->ad_count - 1; i >= 0; i--)
	{
		status = added_keyset[i].status;
		if (0 != (status & CURS_SELF_ADDING))
		{
			status |= CURS_SELF_ADDED;
			status &= ~CURS_SELF_ADDING;
		}
		if (0 != (status & CURS_SELF_UPDATING))
		{
			status |= CURS_SELF_UPDATED;
			status &= ~CURS_SELF_UPDATING;
		}
		if (0 != (status & CURS_SELF_DELETING))
		{
			status |= CURS_SELF_DELETED;
			status &= ~CURS_SELF_DELETING;
		}
		if (status != added_keyset[i].status)
		{
inolog("!!Commit Added=%d(%d)\n", QR_get_num_total_read(res) + i, i);
			added_keyset[i].status = status;
		}
	}
}


int AddDeleted(QResultClass *res, UInt4 index, KeySet *keyset)
{
	int	i;
	Int4	dl_count;
	UInt4	new_alloc;
	UInt4	*deleted;
	KeySet	*deleted_keyset;
	UWORD	status;
	Int4	num_fields = res->num_fields;

inolog("AddDeleted %d\n", index);
	if (!res)	return FALSE;
	dl_count = res->dl_count;
	res->dl_count++;
	if (!QR_get_cursor(res))
		return TRUE;
	if (!res->deleted)
	{
		dl_count = 0;
		new_alloc = 10;
		QR_MALLOC_return_with_error(res->deleted, UInt4, sizeof(UInt4) * new_alloc, res, "Deleted index malloc error", FALSE);
		QR_MALLOC_return_with_error(res->deleted_keyset, KeySet, sizeof(KeySet) * new_alloc, res, "Deleted keyset malloc error", FALSE);
		deleted = res->deleted;
		deleted_keyset = res->deleted_keyset;
		res->dl_alloc = new_alloc;
	}
	else
	{
		if (dl_count >= res->dl_alloc)
		{
			new_alloc = res->dl_alloc * 2;
			res->dl_alloc = 0;
			QR_REALLOC_return_with_error(res->deleted, UInt4, sizeof(UInt4) * new_alloc, res, "Dleted index realloc error", FALSE);
			deleted = res->deleted;
			QR_REALLOC_return_with_error(res->deleted_keyset, KeySet, sizeof(KeySet) * new_alloc, res, "Dleted KeySet realloc error", FALSE);
			deleted_keyset = res->deleted_keyset;
			res->dl_alloc = new_alloc; 
		}
		/* sort deleted indexes in ascending order */
		for (i = 0, deleted = res->deleted, deleted_keyset = res->deleted_keyset; i < dl_count; i++, deleted++, deleted_keyset += num_fields)
		{
			if (index < *deleted)
				break;
		}
		memmove(deleted + 1, deleted, sizeof(UInt4) * (dl_count - i)); 
		memmove(deleted_keyset + 1, deleted_keyset, sizeof(KeySet) * (dl_count - i)); 
	}
	*deleted = index;
	*deleted_keyset = *keyset;
	status = keyset->status;
	status &= (~KEYSET_INFO_PUBLIC);
	status |= SQL_ROW_DELETED;
	if (CC_is_in_trans(QR_get_conn(res)))
	{
		status |= CURS_SELF_DELETING;
		QR_get_conn(res)->result_uncommitted = 1;
	}
	else
	{
		status &= ~(CURS_SELF_ADDING | CURS_SELF_UPDATING | CURS_SELF_DELETING);
		status |= CURS_SELF_DELETED;
	}
	deleted_keyset->status = status;
	res->dl_count = dl_count + 1;

	return TRUE;
}

static void RemoveDeleted(QResultClass *res, Int4 index)
{
	int	i, mv_count, rm_count = 0;
	Int4	pidx, midx;
	UInt4	*deleted, num_read = QR_get_num_total_read(res);
	KeySet	*deleted_keyset;

	mylog("RemoveDeleted index=%d\n", index);
	if (index < 0)
	{
		midx = index;
		pidx = num_read - index - 1;
	}
	else
	{
		pidx = index;
		if (index >= (Int4) num_read)
			midx = num_read - index - 1;
		else
			midx = index;
	}
	for (i = 0; i < res->dl_count; i++)
	{
		if (pidx == res->deleted[i] ||
		    midx == res->deleted[i])
		{
			mv_count = res->dl_count - i - 1;
			if (mv_count > 0)
			{
				deleted = res->deleted + i;
				deleted_keyset = res->deleted_keyset + i;
				memmove(deleted, deleted + 1, mv_count * sizeof(UInt4));
				memmove(deleted_keyset, deleted_keyset + 1, mv_count * sizeof(KeySet));
			}
			res->dl_count--;
			rm_count++;		
		}
	}
	mylog("RemoveDeleted removed count=%d,%d\n", rm_count, res->dl_count);
}

static void CommitDeleted(QResultClass *res)
{
	int	i;
	UInt4	*deleted;
	KeySet	*deleted_keyset;
	UWORD	status;

	if (!res->deleted)
		return;

	for (i = 0, deleted = res->deleted, deleted_keyset = res->deleted_keyset; i < res->dl_count; i++, deleted++, deleted_keyset++)
	{
		status = deleted_keyset->status;
		if (0 != (status & CURS_SELF_ADDING))
		{
			status |= CURS_SELF_ADDED;
			status &= ~CURS_SELF_ADDING;
		}
		if (0 != (status & CURS_SELF_UPDATING))
		{
			status |= CURS_SELF_UPDATED;
			status &= ~CURS_SELF_UPDATING;
		}
		if (0 != (status & CURS_SELF_DELETING))
		{
			status |= CURS_SELF_DELETED;
			status &= ~CURS_SELF_DELETING;
		}
		if (status != deleted_keyset->status)
		{
inolog("!!Commit Deleted=%d(%d)\n", *deleted, i);
			deleted_keyset->status = status;
		}
	} 
}

static BOOL enlargeUpdated(QResultClass *res, Int4 number, const StatementClass *stmt)
{
	Int4	alloc;
	UInt4	*updated;
	KeySet	*updated_keyset;
	TupleField	*updated_tuples = NULL;

	alloc = res->up_alloc;
	if (0 == alloc)
		alloc = number > 10 ? number : 10;
	else
		while (alloc < number)
		{
			alloc *= 2;
		}
	if (alloc <= res->up_alloc)
		return TRUE;
 
	if (updated = realloc(res->updated, sizeof(UInt4) * alloc), !updated)
	{
		if (res->updated_keyset)
		{
			free(res->updated_keyset);
			res->updated_keyset = NULL;
		}
		res->up_alloc = 0;
		return FALSE;
	}
	if (updated_keyset = realloc(res->updated_keyset, sizeof(KeySet) * alloc), !updated_keyset)
	{
		free(res->updated);
		res->updated = NULL;
		res->up_alloc = 0;
		return FALSE;
	}
	if (SQL_CURSOR_KEYSET_DRIVEN != stmt->options.cursor_type)
		if (updated_tuples = realloc(res->updated_tuples, sizeof(TupleField) * res->num_fields * alloc), !updated_tuples)
		{
			free(res->updated);
			res->updated = NULL;
			free(res->updated_keyset);
			res->updated_keyset = NULL;
			res->up_alloc = 0;
			return FALSE;
		}
	res->updated = updated; 
	res->updated_keyset = updated_keyset; 
	res->updated_tuples = updated_tuples;
	res->up_alloc = alloc;

	return TRUE;
}

static void AddUpdated(StatementClass *stmt, int index)
{
	QResultClass	*res;
	UInt4	*updated;
	KeySet	*updated_keyset, *keyset;
	TupleField	*updated_tuples = NULL, *tuple_updated,  *tuple;
	UInt4	kres_ridx, up_count;
	BOOL	is_in_trans;
	int	i, num_fields, upd_idx, upd_add_idx;
	UWORD	status;

inolog("AddUpdated index=%d\n", index);
	if (!stmt)	return;
	if (res = SC_get_Curres(stmt), !res)	return;
	if (!res->keyset)		return;
	kres_ridx = GIdx2KResIdx(index, stmt, res);
	if (kres_ridx < 0 || kres_ridx >= res->num_cached_keys)
		return;
	keyset = res->keyset + kres_ridx;
	if (0 != (keyset->status & CURS_SELF_ADDING))
		AddRollback(stmt, res, index, res->keyset + kres_ridx, SQL_REFRESH);
	if (!QR_get_cursor(res))	return;
	up_count = res->up_count;
	if (up_count > 0 && 0 == res->up_alloc)	return;
	num_fields = res->num_fields;
	tuple_updated = res->backend_tuples + kres_ridx * num_fields;
	if (!tuple_updated)
		return;
	upd_idx = -1;
	upd_add_idx = -1;
	updated = res->updated;
	is_in_trans = CC_is_in_trans(SC_get_conn(stmt));
	updated_keyset = res->updated_keyset;	
	status = keyset->status;
	status &= (~KEYSET_INFO_PUBLIC);
	status |= SQL_ROW_UPDATED;
	if (is_in_trans)
		status |= CURS_SELF_UPDATING;
	else
	{
		for (i = up_count - 1; i >= 0; i--)
		{
			if (updated[i] == index)
				break;
		}
		if (i >= 0)
			upd_idx = i;
		else
		{
			Int4	num_totals = QR_get_num_total_tuples(res);
			if (index >= num_totals)
				upd_add_idx = num_totals - index;
		}
		status |= CURS_SELF_UPDATED;
		status &= ~(CURS_SELF_ADDING | CURS_SELF_UPDATING | CURS_SELF_DELETING);
	}

	tuple = NULL;
	/* update the corresponding add(updat)ed info */
	if (upd_add_idx >= 0)
	{
		res->added_keyset[upd_add_idx].status = status;
		if (res->added_tuples)
		{
			tuple = res->added_tuples + num_fields * upd_add_idx;
			ClearCachedRows(tuple, num_fields, 1);
		}
	}
	else if (upd_idx >= 0)
	{
		res->updated_keyset[upd_idx].status = status;
		if (res->updated_tuples)
		{
			tuple = res->added_tuples + num_fields * upd_add_idx;
			ClearCachedRows(tuple, num_fields, 1);
		}
	}
	else
	{
		if (!enlargeUpdated(res, res->up_count + 1, stmt))
			return;
		updated = res->updated; 
		updated_keyset = res->updated_keyset; 
		updated_tuples = res->updated_tuples;
		upd_idx = up_count;
		updated[up_count] = index;
		updated_keyset[up_count] = *keyset;
		updated_keyset[up_count].status = status;
		if (updated_tuples)
		{
			tuple = updated_tuples + num_fields * up_count;
			memset(tuple, 0, sizeof(TupleField) * num_fields);
		}
		res->up_count++;
	}

	if (tuple)
		ReplaceCachedRows(tuple, tuple_updated, num_fields, 1);
	if (is_in_trans)
		SC_get_conn(stmt)->result_uncommitted = 1;
	mylog("up_count=%d\n", res->up_count);
}

static void RemoveUpdated(QResultClass *res, Int4 index)
{
	mylog("RemoveUpdated index=%d\n", index);
	RemoveUpdatedAfterTheKey(res, index, NULL);
}

static void RemoveUpdatedAfterTheKey(QResultClass *res, Int4 index, const KeySet *keyset)
{
	UInt4	*updated, num_read = QR_get_num_total_read(res);
	KeySet	*updated_keyset;
	TupleField	*updated_tuples = NULL;
	Int4	pidx, midx, mv_count;
	int	i, num_fields = res->num_fields, rm_count = 0;

	mylog("RemoveUpdatedAfterTheKey %d,(%d,%d)\n", index, keyset ? keyset->blocknum : 0, keyset ? keyset->offset : 0);
	if (index < 0)
	{
		midx = index;
		pidx = num_read - index - 1;
	}
	else
	{
		pidx = index;
		if (index >= (Int4)num_read)
			midx = num_read - index - 1;
		else
			midx = index;
	}
	for (i = 0; i < res->up_count; i++)
	{
		updated = res->updated + i;
		if (pidx == *updated ||
		    midx == *updated)
		{
			updated_keyset = res->updated_keyset + i;
			if (keyset &&
			    updated_keyset->blocknum == keyset->blocknum &&
			    updated_keyset->offset == keyset->offset)
				break;
			updated_tuples = NULL;
			if (res->updated_tuples)
			{
				updated_tuples = res->updated_tuples + i * num_fields;
				ClearCachedRows(updated_tuples, num_fields, 1);
			}
			mv_count = res->up_count - i -1;
			if (mv_count > 0)
			{
				memmove(updated, updated + 1, sizeof(UInt4) * mv_count); 
				memmove(updated_keyset, updated_keyset + 1, sizeof(KeySet) * mv_count); 
				if (updated_tuples)
					memmove(updated_tuples, updated_tuples + num_fields, sizeof(TupleField) * num_fields * mv_count);
			}
			res->up_count--;
			rm_count++;
		}
	}
	mylog("RemoveUpdatedAfter removed count=%d,%d\n", rm_count, res->up_count);
}

static void CommitUpdated(QResultClass *res)
{
	KeySet	*updated_keyset;
	TupleField	*updated_tuples = NULL;
	int	i, num_fields = res->num_fields;
	UWORD	status;

	mylog("CommitUpdated res=%x\n", res);
	if (!res)	return;
	if (!QR_get_cursor(res))
		return;
	if (res->up_count <= 0)
		return;
	if (updated_keyset = res->updated_keyset, !updated_keyset)
		return;
	for (i = res->up_count - 1; i >= 0; i--)
	{
		status = updated_keyset[i].status;
		if (0 != (status & CURS_SELF_UPDATING))
		{
			status &= ~CURS_SELF_UPDATING;
			status |= CURS_SELF_UPDATED;
		}
		if (0 != (status & CURS_SELF_ADDING))
		{
			status &= ~CURS_SELF_ADDING;
			status |= CURS_SELF_ADDED;
		}
		if (0 != (status & CURS_SELF_DELETING))
		{
			status &= ~CURS_SELF_DELETING;
			status |= CURS_SELF_DELETED;
		}
		if (status != updated_keyset[i].status)
		{
inolog("!!Commit Updated=%d(%d)\n", res->updated[i], i);
			updated_keyset[i].status = status;
		}
	}
}


static void DiscardRollback(StatementClass *stmt, QResultClass *res)
{
	int	i;
	Int4	index, kres_ridx;
	UWORD	status;
	Rollback *rollback;
	KeySet	*keyset;
	BOOL	kres_is_valid;

inolog("DiscardRollback");
	if (QR_get_cursor(res))
	{
		CommitAdded(res);
		CommitUpdated(res);
		CommitDeleted(res);
		return;
	}

	if (0 == res->rb_count || NULL == res->rollback)
		return;
	rollback = res->rollback;
	keyset = res->keyset;
	for (i = 0; i < res->rb_count; i++)
	{
		index = rollback[i].index;
		status = 0;
		kres_is_valid = FALSE;
		if (index >= 0)
		{
			kres_ridx = GIdx2KResIdx(index, stmt, res);
			if (kres_ridx >= 0 && kres_ridx < (Int4)res->num_cached_keys)
			{
				kres_is_valid = TRUE;
				status = keyset[kres_ridx].status;
			}
		}
		if (kres_is_valid)
		{
			keyset[kres_ridx].status &= ~(CURS_SELF_DELETING | CURS_SELF_UPDATING | CURS_SELF_ADDING);
			keyset[kres_ridx].status |= ((status & (CURS_SELF_DELETING | CURS_SELF_UPDATING | CURS_SELF_ADDING)) << 3);
		}
	}
	free(rollback);
	res->rollback = NULL;
	res->rb_count = res->rb_alloc = 0;
}

static BOOL IndexExists(const StatementClass *stmt, const QResultClass *res, const Rollback *rollback)
{
	Int4	index = rollback->index, i, *updated;
	BOOL	ret = TRUE;

inolog("IndexExists index=%d(%d,%d)\n", rollback->index, rollback->blocknum, rollback->offset);
	if (QR_get_cursor(res))
	{
		KeySet	*updated_keyset = res->updated_keyset, *keyset;
		Int4	num_read = QR_get_num_total_read(res), pidx, midx, marki;

		updated = (Int4 *) res->updated;
		if (!updated || res->up_count < 1)
			return FALSE;
		if (index < 0)
		{
			midx = index;
			pidx = num_read - index - 1;
		}
		else
		{
			pidx = index;
			if (index >= (Int4) num_read)
				midx = num_read - index - 1;
			else
				midx = index;
		}
		for (i = res->up_count - 1, marki = -1; i >= 0; i--)
		{
			if (updated[i] == pidx ||
			    updated[i] == midx)
			{
				keyset = updated_keyset + i;
				if (keyset->blocknum == rollback->blocknum &&
				    keyset->offset == rollback->offset)
					break;
				else
					marki = i;
			}
		}
		if (marki < 0)
			ret = FALSE;
		if (marki >= 0)
		{
			if (!tupleExists(stmt, updated_keyset + marki))
				ret = FALSE;
		}
	}
	return ret;
}

static QResultClass *positioned_load(StatementClass *stmt, UInt4 flag, const UInt4 *oidint, const char *tid);
static void UndoRollback(StatementClass *stmt, QResultClass *res, BOOL partial)
{
	Int4	i, rollbp;
	Int4	index, ridx, kres_ridx;
	UWORD	status;
	Rollback *rollback;
	KeySet	*keyset, keys, *wkey;
	BOOL	curs = (NULL != QR_get_cursor(res)),
		reached_eof = QR_once_reached_eof(res), kres_is_valid, texist;

	if (0 == res->rb_count || NULL == res->rollback)
		return;
	rollback = res->rollback;
	keyset = res->keyset;

	rollbp = 0;
	if (partial)
	{
		Int4	doubtp, rollbps, j, pidx, midx;

		rollbps = rollbp = res->rb_count;
		for (i = 0, doubtp = 0; i < res->rb_count; i++)
		{
			index = rollback[i].index;
			keys.blocknum = rollback[i].blocknum;
			keys.offset = rollback[i].offset;
			texist = tupleExists(stmt, &keys);
inolog("texist[%d]=%d", i, texist);
			if (SQL_ADD == rollback[i].option)
			{
				if (texist)
					doubtp = i + 1;
			}
			else if (SQL_REFRESH == rollback[i].option)
			{
				if (texist || doubtp == i)
					doubtp = i + 1;
			}
			else
			{
				if (texist)
					break;
				if (doubtp == i)
					doubtp = i + 1;
			}
inolog(" doubtp=%d\n", doubtp);
		}
		rollbp = i;
inolog(" doubtp=%d,rollbp=%d\n", doubtp, rollbp);
		if (doubtp < 0)
			doubtp = 0;
		do
		{
			rollbps = rollbp;
			for (i = doubtp; i < rollbp; i++)
			{
				index = rollback[i].index;
				if (SQL_ADD == rollback[i].option)
				{
inolog("index[%d]=%d\n", i, index);
					if (index < 0)
					{
						midx = index;
						pidx = res->num_total_read - index - 1;
					}
					else
					{
						pidx = index;
						midx = res->num_total_read - index - 1;
					}
inolog("pidx=%d,midx=%d\n", pidx, midx); 
					for (j = rollbp - 1; j > i; j--)
					{
						if (rollback[j].index == midx ||
						    rollback[j].index == pidx)
						{
							if (SQL_DELETE == rollback[j].option)
							{
inolog("delete[%d].index=%d\n", j, rollback[j].index);
								break;
							}
							/*else if (SQL_UPDATE == rollback[j].option)
							{
inolog("update[%d].index=%d\n", j, rollback[j].index);
								if (IndexExists(stmt, res, rollback + j))
									break;
							}*/
						}
					}
					if (j <= i)
					{
						rollbp = i;
						break;
					}
				}
			}
		} while (rollbp < rollbps);
	}
inolog("rollbp=%d\n", rollbp);

	for (i = res->rb_count - 1; i >= rollbp; i--)
	{
inolog("UndoRollback %d(%d)\n", i, rollback[i].option);
		index = rollback[i].index;
		if (curs)
		{
			if (SQL_ADD == rollback[i].option)
				RemoveAdded(res, index);
			RemoveDeleted(res, index);
			keys.blocknum = rollback[i].blocknum;
			keys.offset = rollback[i].offset;
			RemoveUpdatedAfterTheKey(res, index, &keys);
		}
		status = 0;
		kres_is_valid = FALSE;
		if (index >= 0)
		{
			kres_ridx = GIdx2KResIdx(index, stmt, res);
			if (kres_ridx >= 0 && kres_ridx < (Int4)res->num_cached_keys)
			{
				kres_is_valid = TRUE;
				wkey = keyset + kres_ridx;
				status = wkey->status;
			}
		}
inolog(" index=%d status=%x", index, status);
		if (kres_is_valid)
		{
			QResultClass	*qres;
			Int4		num_fields = res->num_fields;

			ridx = GIdx2CacheIdx(index, stmt, res);
			if (SQL_ADD == rollback[i].option)
			{
				if (ridx >=0 && ridx < (Int4)res->num_cached_rows)
				{
					TupleField *tuple = res->backend_tuples + res->num_fields * ridx;
					ClearCachedRows(tuple, res->num_fields, 1);
					res->num_cached_rows--;
				}
				res->num_cached_keys--;
				if (!curs)
					res->ad_count--;
			}
			else if (SQL_REFRESH == rollback[i].option)
				continue;
			else
			{
inolog(" (%u, %u)", wkey->blocknum,  wkey->offset);
				wkey->blocknum = rollback[i].blocknum;
				wkey->offset = rollback[i].offset;
inolog("->(%u, %u)\n", wkey->blocknum, wkey->offset);
				wkey->status &= ~KEYSET_INFO_PUBLIC;
				if (SQL_DELETE == rollback[i].option)
					wkey->status &= ~CURS_SELF_DELETING;
				else if (SQL_UPDATE == rollback[i].option)
					wkey->status &= ~CURS_SELF_UPDATING;
				wkey->status |= CURS_NEEDS_REREAD;
				if (ridx >=0 && ridx < (Int4)res->num_cached_rows)
				{
					char	tidval[32];

					sprintf(tidval, "(%d,%d)", wkey->blocknum, wkey->offset);
					qres = positioned_load(stmt, 0, NULL, tidval);
					if (QR_command_maybe_successful(qres) &&
					    QR_get_num_cached_tuples(qres) == 1)
					{
						MoveCachedRows(res->backend_tuples + num_fields * ridx, qres->backend_tuples, num_fields, 1);
						wkey->status &= ~CURS_NEEDS_REREAD;
					}
					QR_Destructor(qres);
				}
			}
		}
	}
	res->rb_count = rollbp;
	if (0 == rollbp)
	{
		free(rollback);
		res->rollback = NULL;
		res->rb_alloc = 0;
	}
}

void	ProcessRollback(ConnectionClass *conn, BOOL undo, BOOL partial) 
{
	int	i;
	StatementClass	*stmt;
	QResultClass	*res;

	for (i = 0; i < conn->num_stmts; i++)
	{
		if (stmt = conn->stmts[i], !stmt)
			continue;
		for (res = SC_get_Result(stmt); res; res = res->next)
		{
			if (undo)
				UndoRollback(stmt, res, partial);
			else
				DiscardRollback(stmt, res);
		}
	}
}


#define	LATEST_TUPLE_LOAD	1L
#define	USE_INSERTED_TID	(1L << 1)
static QResultClass *
positioned_load(StatementClass *stmt, UInt4 flag, const UInt4 *oidint, const char *tidval)
{
	CSTR	func = "positioned_load";
	CSTR	andqual = " and ";
	QResultClass *qres = NULL;
	char	*selstr, oideqstr[256];
	BOOL	latest = ((flag & LATEST_TUPLE_LOAD) != 0);
	UInt4	len;
	TABLE_INFO	*ti = stmt->ti[0];
	const char *bestitem = GET_NAME(ti->bestitem);
	const char *bestqual = GET_NAME(ti->bestqual);

inolog("%s bestitem=%s bestqual=%s\n", func, SAFE_NAME(ti->bestitem), SAFE_NAME(ti->bestqual));
	if (!bestitem || !oidint)
		*oideqstr = '\0';
	else
	{
		/*snprintf(oideqstr, sizeof(oideqstr), " and \"%s\" = %u", bestitem, oid);*/
		strcpy(oideqstr, andqual);
		sprintf(oideqstr + strlen(andqual), bestqual, *oidint);
	}
	len = strlen(stmt->load_statement);
	len += strlen(oideqstr);
	if (tidval)
		len += 100;
	else if ((flag & USE_INSERTED_TID) != 0)
		len += 50;
	else
		len += 20;
	selstr = malloc(len);
	if (tidval)
	{
		if (latest)
		{
			if (NAME_IS_VALID(ti->schema_name))
				snprintf(selstr, len, "%s where ctid = currtid2('\"%s\".\"%s\"', '%s') %s",
				stmt->load_statement, SAFE_NAME(ti->schema_name),
				SAFE_NAME(ti->table_name), tidval, oideqstr);
			else
				snprintf(selstr, len, "%s where ctid = currtid2('%s', '%s') %s", stmt->load_statement, SAFE_NAME(ti->table_name), tidval, oideqstr);
		}
		else 
			snprintf(selstr, len, "%s where ctid = '%s' %s", stmt->load_statement, tidval, oideqstr); 
	}
	else if ((flag & USE_INSERTED_TID) != 0)
		snprintf(selstr, len, "%s where ctid = currtid(0, '(,)') %s", stmt->load_statement, oideqstr);
	else if (bestitem && oidint)
	{
		Int4	slen;
		/*snprintf(selstr, len, "%s where \"%s\" = %u", stmt->load_statement, bestitem, *oid);*/
		snprintf(selstr, len, "%s where ", stmt->load_statement);
		slen = strlen(selstr);
		snprintf(selstr + slen, len - slen, bestqual, *oidint);
	}
	else
	{
		SC_set_error(stmt,STMT_INTERNAL_ERROR, "can't find the add and updating row because of the lack of oid", func);
		goto cleanup;
	} 

	mylog("selstr=%s\n", selstr);
	qres = CC_send_query(SC_get_conn(stmt), selstr, NULL, 0, stmt);
cleanup:
	free(selstr);
	return qres;
}

RETCODE
SC_pos_reload(StatementClass *stmt, SQLULEN global_ridx, UWORD *count, Int4 logKind)
{
	CSTR		func = "SC_pos_reload";
	int		res_cols;
	UWORD		rcnt, offset;
	Int4		res_ridx, kres_ridx;
	UInt4		oidint, blocknum;
	QResultClass	*res, *qres;
	IRDFields	*irdflds = SC_get_IRDF(stmt);
	RETCODE		ret = SQL_ERROR;
	char		tidval[32];
	BOOL		use_ctid = TRUE, data_in_cache = TRUE, key_in_cache = TRUE;

	mylog("positioned load fi=%x ti=%x\n", irdflds->fi, stmt->ti);
	rcnt = 0;
	if (count)
		*count = 0;
	if (!(res = SC_get_Curres(stmt)))
	{
		SC_set_error(stmt, STMT_INVALID_CURSOR_STATE_ERROR, "Null statement result in SC_pos_reload.", func);
		return SQL_ERROR;
	}
	res_ridx = GIdx2CacheIdx(global_ridx, stmt, res);
	if (res_ridx < 0 || res_ridx >= (Int4)QR_get_num_cached_tuples(res))
	{
		data_in_cache = FALSE;
		SC_set_error(stmt, STMT_ROW_OUT_OF_RANGE, "the target rows is out of the rowset", func);
		return SQL_ERROR;
	}
	kres_ridx = GIdx2KResIdx(global_ridx, stmt, res);
	if (kres_ridx < 0 || kres_ridx >= (Int4) res->num_cached_keys)
	{
		key_in_cache = FALSE;
		SC_set_error(stmt, STMT_ROW_OUT_OF_RANGE, "the target rows is out of the rowset", func);
		return SQL_ERROR;
	}
	else if (0 != (res->keyset[kres_ridx].status & CURS_SELF_ADDING))
	{
		use_ctid = FALSE;
		mylog("The tuple is currently being added and can't use ctid\n");
	}	

	if (SC_update_not_ready(stmt))
		parse_statement(stmt, TRUE);	/* not preferable */
	if (!stmt->updatable)
	{
		stmt->options.scroll_concurrency = SQL_CONCUR_READ_ONLY;
		SC_set_error(stmt, STMT_INVALID_OPTION_IDENTIFIER, "the statement is read-only", func);
		return SQL_ERROR;
	}
	if (!(oidint = getOid(res, kres_ridx)))
	{
		if (!strcmp(SAFE_NAME(stmt->ti[0]->bestitem), OID_NAME))
		{
			SC_set_error(stmt, STMT_ROW_VERSION_CHANGED, "the row was already deleted ?", func);
			return SQL_SUCCESS_WITH_INFO;
		}
	}
	getTid(res, kres_ridx, &blocknum, &offset);
	sprintf(tidval, "(%u, %u)", blocknum, offset);
	res_cols = getNumResultCols(res);
	qres = positioned_load(stmt, use_ctid ? LATEST_TUPLE_LOAD : 0, &oidint, use_ctid ? tidval : NULL);
	if (!QR_command_maybe_successful(qres))
	{
		ret = SQL_ERROR;
		SC_replace_error_with_res(stmt, STMT_ERROR_TAKEN_FROM_BACKEND, "positioned_load failed", qres, TRUE);
	}
	else
	{
		TupleField *tuple_old, *tuple_new;
		ConnectionClass	*conn = SC_get_conn(stmt);

		rcnt = QR_get_num_cached_tuples(qres);
		tuple_old = res->backend_tuples + res->num_fields * res_ridx;
		if (0 != logKind && CC_is_in_trans(conn))
			AddRollback(stmt, res, global_ridx, res->keyset + kres_ridx, logKind);
		if (rcnt == 1)
		{
			int	effective_fields = res_cols;

			QR_set_position(qres, 0);
			tuple_new = qres->tupleField;
			if (res->keyset && key_in_cache)
			{
				if (SQL_CURSOR_KEYSET_DRIVEN == stmt->options.cursor_type &&
					strcmp(tuple_new[qres->num_fields - res->num_key_fields].value, tidval))
					res->keyset[kres_ridx].status |= SQL_ROW_UPDATED;
				KeySetSet(tuple_new, qres->num_fields, res->num_key_fields, res->keyset + kres_ridx);
			}
			if (data_in_cache)
				MoveCachedRows(tuple_old, tuple_new, effective_fields, 1); 
			ret = SQL_SUCCESS;
		}
		else
		{
			SC_set_error(stmt, STMT_ROW_VERSION_CHANGED, "the content was deleted after last fetch", func);
			ret = SQL_SUCCESS_WITH_INFO;
			if (stmt->options.cursor_type == SQL_CURSOR_KEYSET_DRIVEN)
			{
				res->keyset[kres_ridx].status |= SQL_ROW_DELETED;
			}
		}
	}
	QR_Destructor(qres);
	if (count)
		*count = rcnt;
	return ret;
}

static	const int	pre_fetch_count = 32;
static int LoadFromKeyset(StatementClass *stmt, QResultClass * res, int rows_per_fetch, int limitrow)
{
	CSTR	func = "LoadFromKeyset";
	ConnectionClass	*conn = SC_get_conn(stmt);
	int	i, j, rowc, rcnt = 0;
	BOOL	prepare;
	UInt4	oid, blocknum, lodlen;
	Int4	kres_ridx;
	UWORD	offset;
	char	*qval = NULL, *sval;
	int	keys_per_fetch = 10;

	prepare = PG_VERSION_GE(conn, 7.3);
	for (i =SC_get_rowset_start(stmt), kres_ridx = GIdx2KResIdx(i, stmt, res), rowc = 0;; i++)
	{
		if (i >= limitrow)
		{
			if (!rowc)
				break;
			if (res->reload_count > 0)
			{
				for (j = rowc; j < keys_per_fetch; j++)
				{
					if (j)
						strcpy(sval, ",NULL");
					else
						strcpy(sval, "NULL");
					sval = strchr(sval, '\0');
				}
			}
			rowc = -1; /* end of loop */
		}
		if (rowc < 0 || rowc >= keys_per_fetch)
		{
			QResultClass	*qres;

			strcpy(sval, ")");
			qres = CC_send_query(conn, qval, NULL, CREATE_KEYSET, stmt);
			if (QR_command_maybe_successful(qres))
			{
				int		j, k, l, m;
				TupleField	*tuple, *tuplew;

				for (j = 0; j < (Int4) QR_get_num_total_read(qres); j++)
				{
					oid = getOid(qres, j); 
					getTid(qres, j, &blocknum, &offset);
					for (k = SC_get_rowset_start(stmt); k < limitrow; k++)
					{
						if (oid == getOid(res, k))
						{
							l = GIdx2CacheIdx(k, stmt, res);
							tuple = res->backend_tuples + res->num_fields * l;
							tuplew = qres->backend_tuples + qres->num_fields * j;
							for (m = 0; m < res->num_fields; m++, tuple++, tuplew++)
							{
								if (tuple->len > 0 && tuple->value)
									free(tuple->value);
								tuple->value = tuplew->value;
								tuple->len = tuplew->len;
								tuplew->value = NULL;
								tuplew->len = -1;
							}
							res->keyset[k].status &= ~CURS_NEEDS_REREAD;
							break;
						}
					}
				}
			}
			else
			{
				SC_set_error(stmt, STMT_EXEC_ERROR, "Data Load Error", func);
				rcnt = -1;
				QR_Destructor(qres);
				break;
			}
			QR_Destructor(qres);
			if (rowc < 0)
				break;
			rowc = 0;
		}
		if (!rowc)
		{
			if (!qval)
			{
				UInt4	allen;

				if (prepare)
				{
					if (res->reload_count > 0)
						keys_per_fetch = res->reload_count;
					else
					{
						char	planname[32];
						int	j;
						QResultClass	*qres;

						if (rows_per_fetch >= pre_fetch_count * 2)
							keys_per_fetch = pre_fetch_count;
						else
							keys_per_fetch = rows_per_fetch;
						if (!keys_per_fetch)
							keys_per_fetch = 2;
						lodlen = strlen(stmt->load_statement);
						sprintf(planname, "_KEYSET_%0x", res);
						allen = 8 + strlen(planname) +
							3 + 4 * keys_per_fetch + 1
							+ 1 + 2 + lodlen + 20 +
							4 * keys_per_fetch + 1;
						SC_MALLOC_return_with_error(qval, char, allen,
							stmt, "Couldn't alloc qval", -1);
						sprintf(qval, "PREPARE \"%s\"", planname);
						sval = strchr(qval, '\0');
						for (j = 0; j < keys_per_fetch; j++)
						{
							if (j == 0)
								strcpy(sval, "(tid");
							else 
								strcpy(sval, ",tid");
							sval = strchr(sval, '\0');
						}
						sprintf(sval, ") as %s where ctid in ", stmt->load_statement);
						sval = strchr(sval, '\0'); 
						for (j = 0; j < keys_per_fetch; j++)
						{
							if (j == 0)
								strcpy(sval, "($1");
							else 
								sprintf(sval, ",$%d", j + 1);
							sval = strchr(sval, '\0');
						}
						strcpy(sval, ")");
						qres = CC_send_query(conn, qval, NULL, 0, stmt);
						if (QR_command_maybe_successful(qres))
						{
							res->reload_count = keys_per_fetch;
						}
						else
						{
							SC_set_error(stmt, STMT_EXEC_ERROR, "Prepare for Data Load Error", func);
							rcnt = -1;
							QR_Destructor(qres);
							break;
						}
						QR_Destructor(qres);
					}
					allen = 25 + 23 * keys_per_fetch;
				}
				else
				{
					keys_per_fetch = pre_fetch_count;
					lodlen = strlen(stmt->load_statement);
					allen = lodlen + 20 + 23 * keys_per_fetch;
				}
				SC_REALLOC_return_with_error(qval, char, allen,
					stmt, "Couldn't alloc qval", -1);
			}
			if (res->reload_count > 0)
			{
				sprintf(qval, "EXECUTE \"_KEYSET_%x\"(", res);
				sval = qval;
			}
			else
			{
				memcpy(qval, stmt->load_statement, lodlen);
				sval = qval + lodlen;
				sval[0]= '\0';
				strcpy(sval, " where ctid in (");
			}
			sval = strchr(sval, '\0');
		}
		if (0 != (res->keyset[kres_ridx].status & CURS_NEEDS_REREAD))
		{
			getTid(res, i, &blocknum, &offset);
			if (rowc)
				sprintf(sval, ",'(%u,%u)'", blocknum, offset);
			else
				sprintf(sval, "'(%u,%u)'", blocknum, offset);
			sval = strchr(sval, '\0');
			rowc++;
			rcnt++;
		}
	}
	if (qval)
		free(qval);
	return rcnt;
}

static RETCODE	SQL_API
SC_pos_reload_needed(StatementClass *stmt, UInt4 req_size, UDWORD flag)
{
	CSTR	func = "SC_pos_reload_needed";
	Int4		i, req_rows_size, limitrow;
	UWORD		qcount;
	QResultClass	*res;
	IRDFields	*irdflds = SC_get_IRDF(stmt);
	RETCODE		ret = SQL_ERROR;
	ConnectionClass	*conn = SC_get_conn(stmt);
	Int4		kres_ridx, rowc, rows_per_fetch;
	BOOL		create_from_scratch = (0 != flag);

	mylog("%s\n", func);
	if (!(res = SC_get_Curres(stmt)))
	{
		SC_set_error(stmt, STMT_INVALID_CURSOR_STATE_ERROR, "Null statement result in SC_pos_reload_needed.", func);
		return SQL_ERROR;
	}
	if (SC_update_not_ready(stmt))
		parse_statement(stmt, TRUE);	/* not preferable */
	if (!stmt->updatable)
	{
		stmt->options.scroll_concurrency = SQL_CONCUR_READ_ONLY;
		SC_set_error(stmt, STMT_INVALID_OPTION_IDENTIFIER, "the statement is read-only", func);
		return SQL_ERROR;
	}
	rows_per_fetch = 0;
	req_rows_size = QR_get_reqsize(res);
	if ((Int4)req_size > req_rows_size)
		req_rows_size = req_size;
	if (create_from_scratch)
	{
		rows_per_fetch = ((pre_fetch_count - 1) / req_rows_size + 1) * req_rows_size;
		limitrow = RowIdx2GIdx(rows_per_fetch, stmt);
	}
	else
	{
		limitrow = RowIdx2GIdx(req_rows_size, stmt);
	}
	if (limitrow > (Int4) res->num_cached_keys)
		limitrow = res->num_cached_keys;
	if (create_from_scratch)
	{
		int	flds_cnt = res->num_cached_rows * res->num_fields,
			brows;

		ClearCachedRows(res->backend_tuples, res->num_fields, res->num_cached_rows);
		brows = GIdx2RowIdx(limitrow, stmt);
		if (brows > (Int4) res->count_backend_allocated)
		{
			res->backend_tuples = realloc(res->backend_tuples, sizeof(TupleField) * res->num_fields * brows);
			res->count_backend_allocated = brows;
		}
		if (brows > 0)
			memset(res->backend_tuples, 0, sizeof(TupleField) * res->num_fields * brows);
		QR_set_num_cached_rows(res, brows);
		QR_set_rowstart_in_cache(res, 0);
		if (SQL_RD_ON != stmt->options.retrieve_data)
			return SQL_SUCCESS;
		for (i = SC_get_rowset_start(stmt), kres_ridx = GIdx2KResIdx(i, stmt,res); i < limitrow; i++, kres_ridx++)
		{
			if (0 == (res->keyset[kres_ridx].status & (CURS_SELF_DELETING | CURS_SELF_DELETED | CURS_OTHER_DELETED)))
				res->keyset[kres_ridx].status |= CURS_NEEDS_REREAD;
		}
	}
	if (rowc = LoadFromKeyset(stmt, res, rows_per_fetch, limitrow), rowc < 0)
	{
		return SQL_ERROR;
	}
	for (i = SC_get_rowset_start(stmt), kres_ridx = GIdx2KResIdx(i, stmt, res); i < limitrow; i++)
	{
		if (0 != (res->keyset[kres_ridx].status & CURS_NEEDS_REREAD))
		{
			ret = SC_pos_reload(stmt, i, &qcount, 0);
			if (SQL_ERROR == ret)
			{
				break;
			}
			if (SQL_ROW_DELETED == (res->keyset[kres_ridx].status & KEYSET_INFO_PUBLIC))
			{
				res->keyset[kres_ridx].status |= CURS_OTHER_DELETED;
			}
			res->keyset[kres_ridx].status &= ~CURS_NEEDS_REREAD;
		}
	}
	return ret;
}

RETCODE		SQL_API
SC_pos_newload(StatementClass *stmt, const UInt4 *oidint, BOOL tidRef)
{
	CSTR	func = "SC_pos_newload";
	int			i;
	QResultClass *res, *qres;
	RETCODE		ret = SQL_ERROR;

	mylog("positioned new ti=%x\n", stmt->ti);
	if (!(res = SC_get_Curres(stmt)))
	{
		SC_set_error(stmt, STMT_INVALID_CURSOR_STATE_ERROR, "Null statement result in SC_pos_newload.", func);
		return SQL_ERROR;
	}
	if (SC_update_not_ready(stmt))
		parse_statement(stmt, TRUE);	/* not preferable */
	if (!stmt->updatable)
	{
		stmt->options.scroll_concurrency = SQL_CONCUR_READ_ONLY;
		SC_set_error(stmt, STMT_INVALID_OPTION_IDENTIFIER, "the statement is read-only", func);
		return SQL_ERROR;
	}
	qres = positioned_load(stmt, tidRef ? USE_INSERTED_TID : 0, oidint, NULL);
	if (!qres || !QR_command_maybe_successful(qres))
	{
		SC_set_error(stmt, STMT_ERROR_TAKEN_FROM_BACKEND, "positioned_load in pos_newload failed", func);
	}
	else
	{
		int		count = QR_get_num_cached_tuples(qres);

		QR_set_position(qres, 0);
		if (count == 1)
		{
			int	effective_fields = res->num_fields;
			int	tuple_size;
			Int4	num_total_rows, num_cached_rows, kres_ridx;
			BOOL	appendKey = FALSE, appendData = FALSE;
			TupleField *tuple_old, *tuple_new;

			tuple_new = qres->tupleField;
			num_total_rows = QR_get_num_total_tuples(res);

			AddAdded(stmt, res, num_total_rows, tuple_new);
			num_cached_rows = QR_get_num_cached_tuples(res);
			kres_ridx = GIdx2KResIdx(num_total_rows, stmt, res);
			if (QR_haskeyset(res))
			{	if (!QR_get_cursor(res))
				{
					appendKey = TRUE;
					if (num_total_rows == CacheIdx2GIdx(num_cached_rows, stmt, res))
						appendData = TRUE;
					else
					{
inolog("total %d <> backend %d - base %d + start %d cursor_type=%d\n", 
num_total_rows, num_cached_rows,
QR_get_rowstart_in_cache(res), SC_get_rowset_start(stmt), stmt->options.cursor_type);
					}
				}
				else if (kres_ridx >= 0 && kres_ridx < (Int4)res->cache_size)
				{
					appendKey = TRUE;
					appendData = TRUE;
				}
			}
			if (appendKey)
			{
			    	if (res->num_cached_keys >= (Int4) res->count_keyset_allocated)
				{
					if (!res->count_keyset_allocated)
						tuple_size = TUPLE_MALLOC_INC;
					else
						tuple_size = res->count_keyset_allocated * 2;
					res->keyset = (KeySet *) realloc(res->keyset, sizeof(KeySet) * tuple_size);	
					res->count_keyset_allocated = tuple_size;
				}
				KeySetSet(tuple_new, qres->num_fields, res->num_key_fields, res->keyset + kres_ridx);
				res->num_cached_keys++;
			}
			if (appendData)
			{
inolog("total %d == backend %d - base %d + start %d cursor_type=%d\n", 
num_total_rows, num_cached_rows,
QR_get_rowstart_in_cache(res), SC_get_rowset_start(stmt), stmt->options.cursor_type);
				if (num_cached_rows >= (Int4)res->count_backend_allocated)
				{
					if (!res->count_backend_allocated)
						tuple_size = TUPLE_MALLOC_INC;
					else
						tuple_size = res->count_backend_allocated * 2;
					res->backend_tuples = (TupleField *) realloc(
						res->backend_tuples,
						res->num_fields * sizeof(TupleField) * tuple_size);
					if (!res->backend_tuples)
					{
						SC_set_error(stmt, QR_set_rstatus(res, PGRES_FATAL_ERROR), "Out of memory while reading tuples.", func);
						QR_Destructor(qres);
						return SQL_ERROR;
					}
					res->count_backend_allocated = tuple_size;
				}
				tuple_old = res->backend_tuples + res->num_fields * num_cached_rows;
				for (i = 0; i < effective_fields; i++)
				{
					tuple_old[i].len = tuple_new[i].len;
					tuple_new[i].len = -1;
					tuple_old[i].value = tuple_new[i].value;
					tuple_new[i].value = NULL;
				}
				res->num_cached_rows++;
			}
			ret = SQL_SUCCESS;
		}
		else if (0 == count)
			ret = SQL_NO_DATA_FOUND;
		else
		{
			SC_set_error(stmt, STMT_ROW_VERSION_CHANGED, "the driver cound't identify inserted rows", func);
			ret = SQL_ERROR;
		}
		/* stmt->currTuple = SC_get_rowset_start(stmt) + ridx; */
	}
	QR_Destructor(qres);
	return ret;
}

static RETCODE SQL_API
irow_update(RETCODE ret, StatementClass *stmt, StatementClass *ustmt, UWORD irow, SQLULEN global_ridx)
{
	CSTR	func = "irow_update";

	if (ret != SQL_ERROR)
	{
		int			updcnt;
		const char *cmdstr = QR_get_command(SC_get_Curres(ustmt));

		if (cmdstr &&
			sscanf(cmdstr, "UPDATE %d", &updcnt) == 1)
		{
			if (updcnt == 1)
			{
				ret = SC_pos_reload(stmt, global_ridx, (UWORD *) 0, SQL_UPDATE);
				if (SQL_ERROR != ret)
					AddUpdated(stmt, global_ridx);
			}
			else if (updcnt == 0)
			{
				SC_set_error(stmt, STMT_ROW_VERSION_CHANGED, "the content was changed before updation", func);
				ret = SQL_ERROR;
				if (stmt->options.cursor_type == SQL_CURSOR_KEYSET_DRIVEN)
					SC_pos_reload(stmt, global_ridx, (UWORD *) 0, 0);
			}
			else
				ret = SQL_ERROR;
		}
		else
			ret = SQL_ERROR;
		if (ret == SQL_ERROR && SC_get_errornumber(stmt) == 0)
		{
			SC_set_error(stmt, STMT_ERROR_TAKEN_FROM_BACKEND, "SetPos update return error", func);
		}
	}
	return ret;
}

/* SQL_NEED_DATA callback for SC_pos_update */
typedef struct
{
	BOOL		updyes;
	QResultClass	*res;
	StatementClass	*stmt, *qstmt;
	IRDFields	*irdflds;
	UWORD		irow;
	SQLULEN		global_ridx;
}	pup_cdata;
static RETCODE
pos_update_callback(RETCODE retcode, void *para)
{
	CSTR	func = "pos_update_callback";
	RETCODE	ret = retcode;
	pup_cdata *s = (pup_cdata *) para;
	Int4	kres_ridx;

	if (s->updyes)
	{
		mylog("pos_update_callback in\n");
		ret = irow_update(ret, s->stmt, s->qstmt, s->irow, s->global_ridx);
inolog("irow_update ret=%d,%d\n", ret, SC_get_errornumber(s->qstmt));
		if (ret != SQL_SUCCESS)
			SC_error_copy(s->stmt, s->qstmt, TRUE);
		PGAPI_FreeStmt(s->qstmt, SQL_DROP);
		s->qstmt = NULL;
	}
	s->updyes = FALSE;
	kres_ridx = GIdx2KResIdx(s->global_ridx, s->stmt, s->res);
	if (kres_ridx < 0 || kres_ridx >= (Int4)s->res->num_cached_keys)
	{
		SC_set_error(s->stmt, STMT_ROW_OUT_OF_RANGE, "the target rows is out of the rowset", func);
inolog("gidx=%d num_keys=%d kresidx=%d\n", s->global_ridx, s->res->num_cached_keys, kres_ridx);
		return SQL_ERROR;
	}
	if (SQL_SUCCESS == ret && s->res->keyset)
	{
		ConnectionClass	*conn = SC_get_conn(s->stmt);

		if (CC_is_in_trans(conn))
		{
			s->res->keyset[kres_ridx].status |= (SQL_ROW_UPDATED  | CURS_SELF_UPDATING);
		}
		else
			s->res->keyset[kres_ridx].status |= (SQL_ROW_UPDATED  | CURS_SELF_UPDATED);
	}
#if (ODBCVER >= 0x0300)
	if (s->irdflds->rowStatusArray)
	{
		switch (ret)
		{
			case SQL_SUCCESS:
				s->irdflds->rowStatusArray[s->irow] = SQL_ROW_UPDATED;
				break;
			default:
				s->irdflds->rowStatusArray[s->irow] = ret;
		}
	}
#endif /* ODBCVER */

	return ret;
}
RETCODE
SC_pos_update(StatementClass *stmt,
			  UWORD irow, SQLULEN global_ridx)
{
	CSTR	func = "SC_pos_update";
	int			i,
				num_cols,
				upd_cols;
	pup_cdata	s;
	ConnectionClass	*conn;
	ARDFields	*opts = SC_get_ARDF(stmt);
	BindInfoClass *bindings = opts->bindings;
	TABLE_INFO	*ti;
	FIELD_INFO	**fi;
	char		updstr[4096];
	RETCODE		ret;
	UInt4	oid, offset, blocknum;
	UInt2	pgoffset;
	Int4	kres_ridx, *used, bind_size = opts->bind_size;

	s.stmt = stmt;
	s.irow = irow;
	s.global_ridx = global_ridx;
	s.irdflds = SC_get_IRDF(s.stmt);
	fi = s.irdflds->fi;
	if (!(s.res = SC_get_Curres(s.stmt)))
	{
		SC_set_error(s.stmt, STMT_INVALID_CURSOR_STATE_ERROR, "Null statement result in SC_pos_update.", func);
		return SQL_ERROR;
	}
	mylog("POS UPDATE %d+%d fi=%x ti=%x\n", s.irow, QR_get_rowstart_in_cache(s.res), fi, s.stmt->ti);
	if (SC_update_not_ready(stmt))
		parse_statement(s.stmt, TRUE);	/* not preferable */
	if (!s.stmt->updatable)
	{
		s.stmt->options.scroll_concurrency = SQL_CONCUR_READ_ONLY;
		SC_set_error(s.stmt, STMT_INVALID_OPTION_IDENTIFIER, "the statement is read-only", func);
		return SQL_ERROR;
	}
	kres_ridx = GIdx2KResIdx(s.global_ridx, s.stmt, s.res);
	if (kres_ridx < 0 || kres_ridx >= (Int4)s.res->num_cached_keys)
	{
		SC_set_error(s.stmt, STMT_ROW_OUT_OF_RANGE, "the target rows is out of the rowset", func);
		return SQL_ERROR;
	}
	if (!(oid = getOid(s.res, kres_ridx)))
	{
		if (!strcmp(SAFE_NAME(stmt->ti[0]->bestitem), OID_NAME))
		{
			SC_set_error(stmt, STMT_ROW_VERSION_CHANGED, "the row was already deleted ?", func);
			return SQL_ERROR;
		}
	}
	getTid(s.res, kres_ridx, &blocknum, &pgoffset);

	ti = s.stmt->ti[0];
	if (NAME_IS_VALID(ti->schema_name))
		sprintf(updstr, "update \"%s\".\"%s\" set", SAFE_NAME(ti->schema_name), SAFE_NAME(ti->table_name));
	else
		sprintf(updstr, "update \"%s\" set", SAFE_NAME(ti->table_name));
	num_cols = s.irdflds->nfields;
	offset = opts->row_offset_ptr ? *opts->row_offset_ptr : 0;
	for (i = upd_cols = 0; i < num_cols; i++)
	{
		if (used = bindings[i].used, used != NULL)
		{
			used += (offset >> 2);
			if (bind_size > 0)
				used += (bind_size * s.irow / 4);
			else	
				used += s.irow; 
			mylog("%d used=%d,%x\n", i, *used, used);
			if (*used != SQL_IGNORE && fi[i]->updatable)
			{
				if (upd_cols)
					sprintf(updstr, "%s, \"%s\" = ?", updstr, GET_NAME(fi[i]->column_name));
				else
					sprintf(updstr, "%s \"%s\" = ?", updstr, GET_NAME(fi[i]->column_name));
				upd_cols++;
			}
		}
		else
			mylog("%d null bind\n", i);
	}
	conn = SC_get_conn(s.stmt);
	s.updyes = FALSE;
	if (upd_cols > 0)
	{
		HSTMT		hstmt;
		int			j;
		int			res_cols = QR_NumResultCols(s.res);
		ConnInfo	*ci = &(conn->connInfo);
		APDFields	*apdopts;
		Int4		fieldtype = 0;
		const char *bestitem = GET_NAME(ti->bestitem);
		const char *bestqual = GET_NAME(ti->bestqual);

		sprintf(updstr, "%s where ctid = '(%u, %u)'", updstr,
				blocknum, pgoffset);
		if (bestitem)
		{
			/*sprintf(updstr, "%s and \"%s\" = %u", updstr, bestitem, oid);*/
			strcat(updstr, " and ");
			sprintf(updstr + strlen(updstr), bestqual, oid);
		}
		mylog("updstr=%s\n", updstr);
		if (PGAPI_AllocStmt(conn, &hstmt) != SQL_SUCCESS)
		{
			SC_set_error(s.stmt, STMT_NO_MEMORY_ERROR, "internal AllocStmt error", func);
			return SQL_ERROR;
		}
		s.qstmt = (StatementClass *) hstmt;
		apdopts = SC_get_APDF(s.qstmt);
		apdopts->param_bind_type = opts->bind_size;
		apdopts->param_offset_ptr = opts->row_offset_ptr;
		SC_set_delegate(s.stmt, s.qstmt);
		for (i = j = 0; i < num_cols; i++)
		{
			if (used = bindings[i].used, used != NULL)
			{
				used += (offset >> 2);
				if (bind_size > 0)
					used += (bind_size * s.irow / 4);
				else
					used += s.irow;
				mylog("%d used=%d\n", i, *used);
				if (*used != SQL_IGNORE && fi[i]->updatable)
				{
					fieldtype = QR_get_field_type(s.res, i);
					PGAPI_BindParameter(hstmt,
						(SQLUSMALLINT) ++j,
						SQL_PARAM_INPUT,
						bindings[i].returntype,
						pgtype_to_concise_type(s.stmt, fieldtype, i),
																fi[i]->column_size > 0 ? fi[i]->column_size : pgtype_column_size(s.stmt, fieldtype, i, ci->drivers.unknown_sizes),
						(SQLSMALLINT) fi[i]->decimal_digits,
						bindings[i].buffer,
						bindings[i].buflen,
						bindings[i].used);
				}
			}
		}
		s.qstmt->exec_start_row = s.qstmt->exec_end_row = s.irow;
		s.updyes = TRUE; 
		ret = PGAPI_ExecDirect(hstmt, updstr, SQL_NTS, 0);
		if (ret == SQL_NEED_DATA)
		{
			pup_cdata *cbdata = (pup_cdata *) malloc(sizeof(pup_cdata));
			memcpy(cbdata, &s, sizeof(pup_cdata));
			enqueueNeedDataCallback(s.stmt, pos_update_callback, cbdata);
			return ret;
		}
		/* else if (ret != SQL_SUCCESS) this is unneccesary 
			SC_error_copy(s.stmt, s.qstmt, TRUE); */
	}
	else
	{
		ret = SQL_SUCCESS_WITH_INFO;
		SC_set_error(s.stmt, STMT_INVALID_CURSOR_STATE_ERROR, "update list null", func);
	}

	ret = pos_update_callback(ret, &s);
	return ret;
}
RETCODE
SC_pos_delete(StatementClass *stmt,
			  UWORD irow, SQLULEN global_ridx)
{
	CSTR	func = "SC_pos_update";
	UWORD		offset;
	QResultClass *res, *qres;
	ConnectionClass	*conn = SC_get_conn(stmt);
	ARDFields	*opts = SC_get_ARDF(stmt);
	IRDFields	*irdflds = SC_get_IRDF(stmt);
	BindInfoClass *bindings = opts->bindings;
	char		dltstr[4096];
	RETCODE		ret;
	Int4		kres_ridx;
	UInt4		oid, blocknum, qflag;
	TABLE_INFO	*ti;
	const char	*bestitem;
	const char	*bestqual;

	mylog("POS DELETE ti=%x\n", stmt->ti);
	if (!(res = SC_get_Curres(stmt)))
	{
		SC_set_error(stmt, STMT_INVALID_CURSOR_STATE_ERROR, "Null statement result in SC_pos_delete.", func);
		return SQL_ERROR;
	}
	if (SC_update_not_ready(stmt))
		parse_statement(stmt, TRUE);	/* not preferable */
	if (!stmt->updatable)
	{
		stmt->options.scroll_concurrency = SQL_CONCUR_READ_ONLY;
		SC_set_error(stmt, STMT_INVALID_OPTION_IDENTIFIER, "the statement is read-only", func);
		return SQL_ERROR;
	}
	kres_ridx = GIdx2KResIdx(global_ridx, stmt, res);
	if (kres_ridx < 0 || kres_ridx >= (Int4)res->num_cached_keys)
	{
		SC_set_error(stmt, STMT_ROW_OUT_OF_RANGE, "the target rows is out of the rowset", func);
		return SQL_ERROR;
	}
	ti = stmt->ti[0];
	bestitem = GET_NAME(ti->bestitem);
	if (!(oid = getOid(res, kres_ridx)))
	{
		if (bestitem && !strcmp(bestitem, OID_NAME))
		{
			SC_set_error(stmt, STMT_ROW_VERSION_CHANGED, "the row was already deleted ?", func);
			return SQL_ERROR;
		}
	}
	bestqual = GET_NAME(ti->bestqual);
	getTid(res, kres_ridx, &blocknum, &offset);
	/*sprintf(dltstr, "delete from \"%s\" where ctid = '%s' and oid = %s",*/
	if (NAME_IS_VALID(ti->schema_name))
		sprintf(dltstr, "delete from \"%s\".\"%s\" where ctid = '(%u, %u)'",
		SAFE_NAME(ti->schema_name), SAFE_NAME(ti->table_name), blocknum, offset);
	else
		sprintf(dltstr, "delete from \"%s\" where ctid = '(%u, %u)'",
			SAFE_NAME(ti->table_name), blocknum, offset);
	if (bestitem)
	{
		/*sprintf(dltstr, "%s and \"%s\" = %u", dltstr, bestitem, oid);*/
		strcat(dltstr, " and ");
		sprintf(dltstr + strlen(dltstr), bestqual, oid);
	}

	mylog("dltstr=%s\n", dltstr);
	qflag = 0;
        if (!stmt->internal && !CC_is_in_trans(conn) &&
                 (!CC_is_in_autocommit(conn)))
		qflag |= GO_INTO_TRANSACTION;
	qres = CC_send_query(conn, dltstr, NULL, qflag, stmt);
	ret = SQL_SUCCESS;
	if (QR_command_maybe_successful(qres))
	{
		int			dltcnt;
		const char *cmdstr = QR_get_command(qres);

		if (cmdstr &&
			sscanf(cmdstr, "DELETE %d", &dltcnt) == 1)
		{
			if (dltcnt == 1)
			{
				RETCODE	tret = SC_pos_reload(stmt, global_ridx, (UWORD *) 0, SQL_DELETE);
				if (SQL_SUCCESS != tret && SQL_SUCCESS_WITH_INFO != tret)
					ret = tret;
			}
			else if (dltcnt == 0)
			{
				SC_set_error(stmt, STMT_ROW_VERSION_CHANGED, "the content was changed before deletion", func);
				ret = SQL_ERROR;
				if (stmt->options.cursor_type == SQL_CURSOR_KEYSET_DRIVEN)
					SC_pos_reload(stmt, global_ridx, (UWORD *) 0, 0);
			}
			else
				ret = SQL_ERROR;
		}
		else
			ret = SQL_ERROR;
	}
	else
		ret = SQL_ERROR;
	if (ret == SQL_ERROR && SC_get_errornumber(stmt) == 0)
	{
		SC_set_error(stmt, STMT_ERROR_TAKEN_FROM_BACKEND, "SetPos delete return error", func);
	}
	if (qres)
		QR_Destructor(qres);
	if (SQL_SUCCESS == ret && res->keyset)
	{
		AddDeleted(res, global_ridx, res->keyset + kres_ridx);
		res->keyset[kres_ridx].status &= (~KEYSET_INFO_PUBLIC);
		if (CC_is_in_trans(conn))
		{
			res->keyset[kres_ridx].status |= (SQL_ROW_DELETED | CURS_SELF_DELETING);
		}
		else
			res->keyset[kres_ridx].status |= (SQL_ROW_DELETED | CURS_SELF_DELETED);
inolog(".status[%d]=%x\n", global_ridx, res->keyset[kres_ridx].status);
	}
#if (ODBCVER >= 0x0300)
	if (irdflds->rowStatusArray)
	{
		switch (ret)
		{
			case SQL_SUCCESS:
				irdflds->rowStatusArray[irow] = SQL_ROW_DELETED;
				break;
			default:
				irdflds->rowStatusArray[irow] = ret;
		}
	}
#endif /* ODBCVER */
	return ret;
}

static RETCODE SQL_API
irow_insert(RETCODE ret, StatementClass *stmt, StatementClass *istmt, Int4 addpos)
{
	CSTR	func = "irow_insert";

	if (ret != SQL_ERROR)
	{
		int		addcnt;
		UInt4		oid, *poid = NULL;
		ARDFields	*opts = SC_get_ARDF(stmt);
		QResultClass	*ires = SC_get_Curres(istmt);
		const char *cmdstr;
		BindInfoClass	*bookmark;

		cmdstr = QR_get_command((ires->next ? ires->next : ires));
		if (cmdstr &&
			sscanf(cmdstr, "INSERT %u %d", &oid, &addcnt) == 2 &&
			addcnt == 1)
		{
			ConnectionClass	*conn = SC_get_conn(stmt);
			RETCODE	qret;

			if (0 != oid)
				poid = &oid;
			qret = SQL_NO_DATA_FOUND;
			if (PG_VERSION_GE(conn, 7.2))
			{
				qret = SC_pos_newload(stmt, poid, TRUE);
				if (SQL_ERROR == qret)
					return qret;
			}
			if (SQL_NO_DATA_FOUND == qret)
			{
				qret = SC_pos_newload(stmt, poid, FALSE);
				if (SQL_ERROR == qret)
					return qret;
			}
			bookmark = opts->bookmark;
			if (bookmark && bookmark->buffer)
			{
				char	buf[32];
				UInt4	offset = opts->row_offset_ptr ? *opts->row_offset_ptr : 0;

				snprintf(buf, sizeof(buf), "%ld", SC_make_bookmark(addpos));
				SC_set_current_col(stmt, -1);
				copy_and_convert_field(stmt,
					PG_TYPE_INT4,
					buf,
                         		bookmark->returntype,
					bookmark->buffer + offset,
					bookmark->buflen,
					bookmark->used ? bookmark->used
					+ (offset >> 2) : NULL);
			}
		}
		else
		{
			SC_set_error(stmt, STMT_ERROR_TAKEN_FROM_BACKEND, "SetPos insert return error", func);
		}
	}
	return ret;
}

/* SQL_NEED_DATA callback for SC_pos_add */
typedef struct
{
	BOOL		updyes;
	QResultClass	*res;
	StatementClass	*stmt, *qstmt;
	IRDFields	*irdflds;
	UWORD		irow;
}	padd_cdata;

static RETCODE
pos_add_callback(RETCODE retcode, void *para)
{
	RETCODE	ret = retcode;
	padd_cdata *s = (padd_cdata *) para;
	Int4	addpos;

	if (s->updyes)
	{
		int	brow_save;
		
		mylog("pos_add_callback in ret=%d\n", ret);
		brow_save = s->stmt->bind_row; 
		s->stmt->bind_row = s->irow;
		if (QR_get_cursor(s->res))
			addpos = -(Int4)(s->res->ad_count + 1);
		else
			addpos = QR_get_num_total_tuples(s->res); 
		ret = irow_insert(ret, s->stmt, s->qstmt, addpos);
		s->stmt->bind_row = brow_save;
	}
	s->updyes = FALSE;
	if (ret != SQL_SUCCESS)
		SC_error_copy(s->stmt, s->qstmt, TRUE);
	PGAPI_FreeStmt((HSTMT) s->qstmt, SQL_DROP);
	s->qstmt = NULL;
	if (SQL_SUCCESS == ret && s->res->keyset)
	{
		int	global_ridx = QR_get_num_total_tuples(s->res) - 1;
		ConnectionClass	*conn = SC_get_conn(s->stmt);
		Int4	kres_ridx;
		UWORD	status = SQL_ROW_ADDED;

		if (CC_is_in_trans(conn))
			status |= CURS_SELF_ADDING;
		else
			status |= CURS_SELF_ADDED;
		kres_ridx = GIdx2KResIdx(global_ridx, s->stmt, s->res);
		if (kres_ridx >= 0 || kres_ridx < (Int4)s->res->num_cached_keys)
		{
			s->res->keyset[kres_ridx].status = status;
		}
	}
#if (ODBCVER >= 0x0300)
	if (s->irdflds->rowStatusArray)
	{
		switch (ret)
		{
			case SQL_SUCCESS:
				s->irdflds->rowStatusArray[s->irow] = SQL_ROW_ADDED;
				break;
			default:
				s->irdflds->rowStatusArray[s->irow] = ret;
		}
	}
#endif /* ODBCVER */

	return ret;
}

RETCODE
SC_pos_add(StatementClass *stmt,
		   UWORD irow)
{
	CSTR	func = "SC_pos_add";
	int			num_cols,
				add_cols,
				i;
	HSTMT		hstmt;

	padd_cdata	s;
	ConnectionClass	*conn;
	ConnInfo	*ci;
	ARDFields	*opts = SC_get_ARDF(stmt);
	APDFields	*apdopts;
	BindInfoClass *bindings = opts->bindings;
	FIELD_INFO	**fi = SC_get_IRDF(stmt)->fi;
	char		addstr[4096];
	RETCODE		ret;
	UInt4		offset;
	Int4		*used, bind_size = opts->bind_size;
	Int4		fieldtype;
	int		func_cs_count = 0;

	mylog("POS ADD fi=%x ti=%x\n", fi, stmt->ti);
	s.stmt = stmt;
	s.irow = irow;
	if (!(s.res = SC_get_Curres(s.stmt)))
	{
		SC_set_error(s.stmt, STMT_INVALID_CURSOR_STATE_ERROR, "Null statement result in SC_pos_add.", func);
		return SQL_ERROR;
	}
	if (SC_update_not_ready(stmt))
		parse_statement(s.stmt, TRUE);	/* not preferable */
	if (!s.stmt->updatable)
	{
		s.stmt->options.scroll_concurrency = SQL_CONCUR_READ_ONLY;
		SC_set_error(s.stmt, STMT_INVALID_OPTION_IDENTIFIER, "the statement is read-only", func);
		return SQL_ERROR;
	}
	s.irdflds = SC_get_IRDF(s.stmt);
	num_cols = s.irdflds->nfields;
	conn = SC_get_conn(s.stmt);
	if (NAME_IS_VALID(s.stmt->ti[0]->schema_name))
		sprintf(addstr, "insert into \"%s\".\"%s\" (", SAFE_NAME(s.stmt->ti[0]->schema_name), SAFE_NAME(s.stmt->ti[0]->table_name));
	else
		sprintf(addstr, "insert into \"%s\" (", SAFE_NAME(s.stmt->ti[0]->table_name));
	if (PGAPI_AllocStmt(conn, &hstmt) != SQL_SUCCESS)
	{
		SC_set_error(s.stmt, STMT_NO_MEMORY_ERROR, "internal AllocStmt error", func);
		return SQL_ERROR;
	}
	if (opts->row_offset_ptr)
		offset = *opts->row_offset_ptr;
	else
		offset = 0;
	s.qstmt = (StatementClass *) hstmt;
	apdopts = SC_get_APDF(s.qstmt);
	apdopts->param_bind_type = opts->bind_size;
	apdopts->param_offset_ptr = opts->row_offset_ptr;
	SC_set_delegate(s.stmt, s.qstmt);
	ci = &(conn->connInfo);
	for (i = add_cols = 0; i < num_cols; i++)
	{
		if (used = bindings[i].used, used != NULL)
		{
			used += (offset >> 2);
			if (bind_size > 0)
				used += (bind_size * s.irow / 4);
			else
				used += s.irow;
			mylog("%d used=%d\n", i, *used);
			if (*used != SQL_IGNORE && fi[i]->updatable)
			{
				fieldtype = QR_get_field_type(s.res, i);
				if (add_cols)
					sprintf(addstr, "%s, \"%s\"", addstr, GET_NAME(fi[i]->column_name));
				else
					sprintf(addstr, "%s\"%s\"", addstr, GET_NAME(fi[i]->column_name));
				PGAPI_BindParameter(hstmt,
					(SQLUSMALLINT) ++add_cols,
					SQL_PARAM_INPUT,
					bindings[i].returntype,
					pgtype_to_concise_type(s.stmt, fieldtype, i),
															fi[i]->column_size > 0 ? fi[i]->column_size : pgtype_column_size(s.stmt, fieldtype, i, ci->drivers.unknown_sizes),
					(SQLSMALLINT) fi[i]->decimal_digits,
					bindings[i].buffer,
					bindings[i].buflen,
					bindings[i].used);
			}
		}
		else
			mylog("%d null bind\n", i);
	}
	s.updyes = FALSE;
#define	return	DONT_CALL_RETURN_FROM_HERE???
	ENTER_INNER_CONN_CS(conn, func_cs_count); 
	if (add_cols > 0)
	{
		sprintf(addstr, "%s) values (", addstr);
		for (i = 0; i < add_cols; i++)
		{
			if (i)
				strcat(addstr, ", ?");
			else
				strcat(addstr, "?");
		}
		strcat(addstr, ")");
		mylog("addstr=%s\n", addstr);
		s.qstmt->exec_start_row = s.qstmt->exec_end_row = s.irow;
		s.updyes = TRUE;
		ret = PGAPI_ExecDirect(hstmt, addstr, SQL_NTS, 0);
		if (ret == SQL_NEED_DATA)
		{
			padd_cdata *cbdata = (padd_cdata *) malloc(sizeof(padd_cdata));
			memcpy(cbdata, &s, sizeof(padd_cdata));
			enqueueNeedDataCallback(s.stmt, pos_add_callback, cbdata);
			goto cleanup;
		}
		/* else if (ret != SQL_SUCCESS) this is unneccesary
			SC_error_copy(s.stmt, s.qstmt, TRUE); */
	}
	else
	{
		ret = SQL_SUCCESS_WITH_INFO;
		SC_set_error(s.stmt, STMT_INVALID_CURSOR_STATE_ERROR, "insert list null", func);
	}

	ret = pos_add_callback(ret, &s);

cleanup:
#undef	return
	CLEANUP_FUNC_CONN_CS(func_cs_count, conn);
	return ret;
}

/*
 *	Stuff for updatable cursors end.
 */

RETCODE
SC_pos_refresh(StatementClass *stmt, UWORD irow , SQLULEN global_ridx)
{
	RETCODE	ret;
#if (ODBCVER >= 0x0300)
	IRDFields	*irdflds = SC_get_IRDF(stmt);
#endif /* ODBCVER */
	/* save the last_fetch_count */
	int		last_fetch = stmt->last_fetch_count;
	int		last_fetch2 = stmt->last_fetch_count_include_ommitted;
	int		bind_save = stmt->bind_row;
	BOOL		tuple_reload = FALSE;

	if (stmt->options.cursor_type == SQL_CURSOR_KEYSET_DRIVEN)
		tuple_reload = TRUE;
	else 
	{
		QResultClass	*res = SC_get_Curres(stmt);
		if (res && res->keyset)
		{
			Int4 kres_ridx = GIdx2KResIdx(global_ridx, stmt, res);
			if (kres_ridx >= 0 && kres_ridx < (Int4)QR_get_num_cached_tuples(res))
			{
				if (0 != (CURS_NEEDS_REREAD & res->keyset[kres_ridx].status))
					tuple_reload = TRUE;
			}
		}
	}
	if (tuple_reload)
		SC_pos_reload(stmt, global_ridx, (UWORD *) 0, 0);
	stmt->bind_row = irow;
	ret = SC_fetch(stmt);
	/* restore the last_fetch_count */
	stmt->last_fetch_count = last_fetch;
	stmt->last_fetch_count_include_ommitted = last_fetch2;
	stmt->bind_row = bind_save;
#if (ODBCVER >= 0x0300)
	if (irdflds->rowStatusArray)
	{
		switch (ret)
		{
			case SQL_ERROR:
				irdflds->rowStatusArray[irow] = SQL_ROW_ERROR;
				break;
			case SQL_SUCCESS:
				irdflds->rowStatusArray[irow] = SQL_ROW_SUCCESS;
				break;
			case SQL_SUCCESS_WITH_INFO:
			default:
				irdflds->rowStatusArray[irow] = ret;
				break;
		}
	}
#endif /* ODBCVER */

	return SQL_SUCCESS;
}

/*	SQL_NEED_DATA callback for PGAPI_SetPos */
typedef struct
{
	BOOL		need_data_callback, auto_commit_needed;
	QResultClass	*res;
	StatementClass	*stmt;
	ARDFields	*opts;
	GetDataInfo	*gdata;
	int	idx, start_row, end_row, processed, ridx;
	UWORD	fOption, irow, nrow;
}	spos_cdata;
static 
RETCODE spos_callback(RETCODE retcode, void *para)
{
	RETCODE	ret;
	spos_cdata *s = (spos_cdata *) para;
	ConnectionClass	*conn;
	SQLULEN	global_ridx;
	Int4	kres_ridx, pos_ridx;

	ret = retcode;
	if (s->need_data_callback)
	{
		mylog("spos_callback in\n");
		s->processed++;
		if (SQL_ERROR != retcode)
		{
			s->nrow++;
			s->idx++;
		}
	}
	else
	{
		s->ridx = -1;
		s->idx = s->nrow = s->processed = 0;
	}
	s->need_data_callback = FALSE;
	for (; SQL_ERROR != ret && s->nrow <= s->end_row; s->idx++)
	{
		global_ridx = RowIdx2GIdx(s->idx, s->stmt);
		if (SQL_ADD != s->fOption)
		{
			if ((int) global_ridx >= QR_get_num_total_tuples(s->res))
				break;
			if (s->res->keyset)
			{
				kres_ridx = GIdx2KResIdx(global_ridx, s->stmt, s->res);
				if (kres_ridx >= (Int4)s->res->num_cached_keys)
					break;
				if (kres_ridx >= 0) /* the row may be deleted and not in the rowset */
				{
					if (0 == (s->res->keyset[kres_ridx].status & CURS_IN_ROWSET))
						continue;
				}
			}
		}
		if (s->nrow < s->start_row)
		{
			s->nrow++;
			continue;
		}	
		s->ridx = s->nrow;
		pos_ridx = s->idx;
#if (ODBCVER >= 0x0300)
		if (0 != s->irow || !s->opts->row_operation_ptr || s->opts->row_operation_ptr[s->nrow] == SQL_ROW_PROCEED)
		{
#endif /* ODBCVER */
			switch (s->fOption)
			{
				case SQL_UPDATE:
					ret = SC_pos_update(s->stmt, s->nrow, global_ridx);
					break;
				case SQL_DELETE:
					ret = SC_pos_delete(s->stmt, s->nrow, global_ridx);
					break;
				case SQL_ADD:
					ret = SC_pos_add(s->stmt, s->nrow);
					break;
				case SQL_REFRESH:
					ret = SC_pos_refresh(s->stmt, s->nrow, global_ridx);
					break;
			}
			if (SQL_NEED_DATA == ret)
			{
				spos_cdata *cbdata = (spos_cdata *) malloc(sizeof(spos_cdata));

				memcpy(cbdata, s, sizeof(spos_cdata));
				cbdata->need_data_callback = TRUE;
				enqueueNeedDataCallback(s->stmt, spos_callback, cbdata);
				return ret;
			}
			s->processed++;
#if (ODBCVER >= 0x0300)
		}
#endif /* ODBCVER */
		if (SQL_ERROR != ret)
			s->nrow++;
	}
	conn = SC_get_conn(s->stmt);
#ifdef	_LEGACY_MODE_
	if (SQL_ERROR == ret)
		CC_abort(conn);
#endif /* _LEGACY_MODE_ */
	if (s->auto_commit_needed)
		PGAPI_SetConnectOption(conn, SQL_AUTOCOMMIT, SQL_AUTOCOMMIT_ON);
	if (s->irow > 0)
	{
		if (SQL_ADD != s->fOption && s->ridx >= 0) /* for SQLGetData */
		{
			s->stmt->currTuple = RowIdx2GIdx(pos_ridx, s->stmt);
			QR_set_position(s->res, pos_ridx);
		}
	}
	else if (SC_get_IRDF(s->stmt)->rowsFetched)
		*(SC_get_IRDF(s->stmt)->rowsFetched) = s->processed;
	s->res->recent_processed_row_count = s->stmt->diag_row_count = s->processed;
inolog("processed=%d ret=%d rowset=%d", s->processed, ret, s->opts->size_of_rowset_odbc2);
#if (ODBCVER >= 0x0300)
inolog(",%d\n", s->opts->size_of_rowset);
#else
inolog("\n");
#endif /* ODBCVER */
	return ret;
}
 
/*
 *	This positions the cursor within a rowset, that was positioned using SQLExtendedFetch.
 *	This will be useful (so far) only when using SQLGetData after SQLExtendedFetch.
 */
RETCODE		SQL_API
PGAPI_SetPos(
			 HSTMT hstmt,
			 SQLSETPOSIROW irow,
			 SQLUSMALLINT fOption,
			 SQLUSMALLINT fLock)
{
	CSTR func = "PGAPI_SetPos";
	RETCODE	ret;
	ConnectionClass	*conn;
	int		num_cols, i, rowsetSize;
	GetDataClass	*gdata = NULL;
	spos_cdata	s;

	s.stmt = (StatementClass *) hstmt;
	if (!s.stmt)
	{
		SC_log_error(func, "", NULL);
		return SQL_INVALID_HANDLE;
	}

	s.irow = irow;
	s.fOption = fOption;
	s.auto_commit_needed = FALSE;
	s.opts = SC_get_ARDF(s.stmt);
	gdata = SC_get_GDTI(s.stmt)->gdata;
	mylog("%s fOption=%d irow=%d lock=%d currt=%d\n", func, s.fOption, s.irow, fLock, s.stmt->currTuple);
	if (s.stmt->options.scroll_concurrency != SQL_CONCUR_READ_ONLY)
		;
	else if (s.fOption != SQL_POSITION && s.fOption != SQL_REFRESH)
	{
		SC_set_error(s.stmt, STMT_NOT_IMPLEMENTED_ERROR, "Only SQL_POSITION/REFRESH is supported for PGAPI_SetPos", func);
		return SQL_ERROR;
	}

	if (!(s.res = SC_get_Curres(s.stmt)))
	{
		SC_set_error(s.stmt, STMT_INVALID_CURSOR_STATE_ERROR, "Null statement result in PGAPI_SetPos.", func);
		return SQL_ERROR;
	}

#if (ODBCVER >= 0x0300)
	rowsetSize = (s.stmt->transition_status == 7 ? s.opts->size_of_rowset_odbc2 : s.opts->size_of_rowset);
#else
	rowsetSize = s.opts->size_of_rowset_odbc2;
#endif /* ODBCVER */
	if (s.irow == 0) /* bulk operation */
	{
		if (SQL_POSITION == s.fOption)
		{
			SC_set_error(s.stmt, STMT_INVALID_CURSOR_POSITION, "Bulk Position operations not allowed.", func);
			return SQL_ERROR;
		}
		s.start_row = 0;
		s.end_row = rowsetSize - 1;
	}
	else
	{
		if (SQL_ADD != s.fOption && s.irow > s.stmt->last_fetch_count)
		{
			SC_set_error(s.stmt, STMT_ROW_OUT_OF_RANGE, "Row value out of range", func);
			return SQL_ERROR;
		}
		s.start_row = s.end_row = s.irow - 1;
	}

	num_cols = QR_NumResultCols(s.res);
	/* Reset for SQLGetData */
	if (gdata)
		for (i = 0; i < num_cols; i++)
			gdata[i].data_left = -1;
	ret = SQL_SUCCESS;
	conn = SC_get_conn(s.stmt);
	switch (s.fOption)
	{
		case SQL_UPDATE:
		case SQL_DELETE:
		case SQL_ADD:
			if (s.auto_commit_needed = CC_is_in_autocommit(conn), s.auto_commit_needed)
				PGAPI_SetConnectOption(conn, SQL_AUTOCOMMIT, SQL_AUTOCOMMIT_OFF);
			break;
		case SQL_POSITION:
			break;
	}

	s.need_data_callback = FALSE;
#define	return	DONT_CALL_RETURN_FROM_HERE???
	/* StartRollbackState(s.stmt); */
	ret = spos_callback(SQL_SUCCESS, &s);
#undef	return
	if (s.stmt->internal)
		ret = DiscardStatementSvp(s.stmt, ret, FALSE);
	return ret;

#ifdef	NOT_USED
	ridx = -1;
	for (i = nrow = 0, processed = 0; nrow <= end_row; i++)
	{
		global_ridx = RowIdx2GIdx(i, stmt);
		if (SQL_ADD != fOption)
		{
			if ((int) global_ridx >= res->num_total_rows)
				break;
			if (res->keyset) /* the row may be deleted and not in the rowset */
			{
				if (0 == (res->keyset[kres_ridx].status & CURS_IN_ROWSET))
					continue;
			}
		}
		if (nrow < start_row)
		{
			nrow++;
			continue;
		}
		ridx = nrow;
#if (ODBCVER >= 0x0300)
		if (0 != irow || !opts->row_operation_ptr || opts->row_operation_ptr[nrow] == SQL_ROW_PROCEED)
		{
#endif /* ODBCVER */
			switch (fOption)
			{
				case SQL_UPDATE:
					ret = SC_pos_update(stmt, nrow, global_ridx);
					break;
				case SQL_DELETE:
					ret = SC_pos_delete(stmt, nrow, global_ridx);
					break;
				case SQL_ADD:
					ret = SC_pos_add(stmt, nrow);
					break;
				case SQL_REFRESH:
					ret = SC_pos_refresh(stmt, nrow, global_ridx);
					break;
			}
			if (SQL_NEED_DATA == ret)
				return ret;
			processed++;
			if (SQL_ERROR == ret)
				break;
#if (ODBCVER >= 0x0300)
		}
#endif /* ODBCVER */
		nrow++;
	}
#ifdef	_LEGACY_MODE_
	if (SQL_ERROR == ret)
		CC_abort(conn);
#endif /* _LEGACY_MODE_ */
	if (auto_commit_needed)
		PGAPI_SetConnectOption(conn, SQL_AUTOCOMMIT, SQL_AUTOCOMMIT_ON);
	if (irow > 0)
	{
		if (SQL_ADD != fOption && ridx >= 0) /* for SQLGetData */
		{ 
			stmt->currTuple = RowIdx2GIdx(ridx, stmt);
			QR_set_position(res, ridx);
		}
	}
	else if (SC_get_IRDF(stmt)->rowsFetched)
		*(SC_get_IRDF(stmt)->rowsFetched) = processed;
	res->recent_processed_row_count = stmt->diag_row_count = processed;
inolog("rowset=%d processed=%d ret=%d\n", opts->rowset_size, processed, ret);
	return ret;
#endif /* NOT_USED */ 
}


/*		Sets options that control the behavior of cursors. */
RETCODE		SQL_API
PGAPI_SetScrollOptions( HSTMT hstmt,
				SQLUSMALLINT fConcurrency,
				SQLLEN crowKeyset,
				SQLUSMALLINT crowRowset)
{
	CSTR func = "PGAPI_SetScrollOptions";
	StatementClass *stmt = (StatementClass *) hstmt;

	mylog("PGAPI_SetScrollOptions fConcurrency=%d crowKeyset=%d crowRowset=%d\n",
		  fConcurrency, crowKeyset, crowRowset);
	SC_set_error(stmt, STMT_NOT_IMPLEMENTED_ERROR, "SetScroll option not implemeted", func);

	return SQL_ERROR;
}


/*	Set the cursor name on a statement handle */
RETCODE		SQL_API
PGAPI_SetCursorName(
				HSTMT hstmt,
				const SQLCHAR FAR * szCursor,
				SQLSMALLINT cbCursor)
{
	CSTR func = "PGAPI_SetCursorName";
	StatementClass *stmt = (StatementClass *) hstmt;
	int			len;

	mylog("PGAPI_SetCursorName: hstmt=%x, szCursor=%x, cbCursorMax=%d\n", hstmt, szCursor, cbCursor);

	if (!stmt)
	{
		SC_log_error(func, "", NULL);
		return SQL_INVALID_HANDLE;
	}

	len = (cbCursor == SQL_NTS) ? strlen(szCursor) : cbCursor;

	SET_NAME(stmt->cursor_name, make_string(szCursor, cbCursor, NULL, 0));
	return SQL_SUCCESS;
}


/*	Return the cursor name for a statement handle */
RETCODE		SQL_API
PGAPI_GetCursorName(
					HSTMT hstmt,
					SQLCHAR FAR * szCursor,
					SQLSMALLINT cbCursorMax,
					SQLSMALLINT FAR * pcbCursor)
{
	CSTR func = "PGAPI_GetCursorName";
	StatementClass *stmt = (StatementClass *) hstmt;
	int			len = 0;
	RETCODE		result;

	mylog("PGAPI_GetCursorName: hstmt=%x, szCursor=%x, cbCursorMax=%d, pcbCursor=%x\n", hstmt, szCursor, cbCursorMax, pcbCursor);

	if (!stmt)
	{
		SC_log_error(func, "", NULL);
		return SQL_INVALID_HANDLE;
	}
	result = SQL_SUCCESS;
	len = strlen(SC_cursor_name(stmt));

	if (szCursor)
	{
		strncpy_null(szCursor, SC_cursor_name(stmt), cbCursorMax);

		if (len >= cbCursorMax)
		{
			result = SQL_SUCCESS_WITH_INFO;
			SC_set_error(stmt, STMT_TRUNCATED, "The buffer was too small for the GetCursorName.", func);
		}
	}

	if (pcbCursor)
		*pcbCursor = len;

	/*
	 * Because this function causes no db-access, there's
	 * no need to call DiscardStatementSvp()
	 */

	return result;
}
