/* File:			catfunc.h
 *
 * Description:		See "info.c"
 *
 * Comments:		See "notice.txt" for copyright and license information.
 *
 */

#ifndef __CATFUNC_H__
#define __CATFUNC_H__

#include "psqlodbc.h"

/*	SQLColumns field position	*/
enum {
	COLUMNS_CATALOG_NAME	=	0
	,COLUMNS_SCHEMA_NAME
	,COLUMNS_TABLE_NAME
	,COLUMNS_COLUMN_NAME
	,COLUMNS_DATA_TYPE
	,COLUMNS_TYPE_NAME
	,COLUMNS_PRECISION
	,COLUMNS_LENGTH
	,COLUMNS_SCALE
	,COLUMNS_RADIX
	,COLUMNS_NULLABLE
	,COLUMNS_REMARKS
#if (ODBCVER >= 0x0300)
	,COLUMNS_COLUMN_DEF
	,COLUMNS_SQL_DATA_TYPE
	,COLUMNS_SQL_DATETIME_SUB
	,COLUMNS_CHAR_OCTET_LENGTH
	,COLUMNS_ORDINAL_POSITION
	,COLUMNS_IS_NULLABLE
#endif	/* ODBCVER */
	,COLUMNS_DISPLAY_SIZE
	,COLUMNS_FIELD_TYPE
};
/* SQLColAttribute */
enum {
	COLATTR_DESC_COUNT = -1
	,COLATTR_DESC_AUTO_UNIQUE_VALUE = 0
	,COLATTR_DESC_BASE_COLUMN_NAME
	,COLATTR_DESC_BASE_TABLE_NAME
	,COLATTR_DESC_CASE_SENSITIVE
	,COLATTR_DESC_CATALOG_NAME
	,COLATTR_DESC_CONCISE_TYPE
	,COLATTR_DESC_DISPLAY_SIZE
	,COLATTR_DESC_FIXED_PREC_SCALE
	,COLATTR_DESC_LABEL
	,COLATTR_DESC_LENGTH
	,COLATTR_DESC_LITERAL_PREFIX
	,COLATTR_DESC_LITERAL_SUFFIX
	,COLATTR_DESC_LOCAL_TYPE_NAME
	,COLATTR_DESC_NAME
	,COLATTR_DESC_NULLABLE
	,COLATTR_DESC_NUM_PREX_RADIX
	,COLATTR_DESC_OCTET_LENGTH
	,COLATTR_DESC_PRECISION
	,COLATTR_DESC_SCALE
	,COLATTR_DESC_SCHEMA_NAME
	,COLATTR_DESC_SEARCHABLE
	,COLATTR_DESC_TABLE_NAME
	,COLATTR_DESC_TYPE
	,COLATTR_DESC_TYPE_NAME
	,COLATTR_DESC_UNNAMED
	,COLATTR_DESC_UNSIGNED
	,COLATTR_DESC_UPDATABLE
};
#endif /* __CARFUNC_H__ */
