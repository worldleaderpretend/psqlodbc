/* File:			tuple.h
 *
 * Description:		See "tuple.c"
 *
 * Important NOTE:	The TupleField structure is used both to hold backend
			data and manual result set data. The "set_" functions
			are only used for manual result sets by info routines.
 *
 * Comments:		See "notice.txt" for copyright and license information.
 *
 */

#ifndef __TUPLE_H__
#define __TUPLE_H__

#include "psqlodbc.h"

/*	Used by backend data AND manual result sets */
struct TupleField_
{
	Int4		len;			/* length of the current Tuple */
	void	   *value;			/* an array representing the value */
};

/*	keyset(TID + OID) info */
struct KeySet_
{
	UWORD	status;
	UWORD	offset;
	UDWORD	blocknum;
	UDWORD	oid;
};
/*	Rollback(index + original TID) info */
struct Rollback_
{
	DWORD	index;
	UDWORD	blocknum;
	UWORD	offset;
	UWORD	option;
};
#define	KEYSET_INFO_PUBLIC	0x07
#define	CURS_SELF_ADDING	(1L << 3)
#define	CURS_SELF_DELETING	(1L << 4)
#define	CURS_SELF_UPDATING	(1L << 5)
#define	CURS_SELF_ADDED		(1L << 6)
#define	CURS_SELF_DELETED	(1L << 7)
#define	CURS_SELF_UPDATED	(1L << 8)
#define	CURS_NEEDS_REREAD	(1L << 9)
#define	CURS_IN_ROWSET		(1L << 10)
#define	CURS_OTHER_DELETED	(1L << 11)

/*	These macros are wrappers for the corresponding set_tuplefield functions
	but these handle automatic NULL determination and call set_tuplefield_null()
	if appropriate for the datatype (used by SQLGetTypeInfo).
*/
#define set_nullfield_string(FLD, VAL)		((VAL) ? set_tuplefield_string(FLD, (VAL)) : set_tuplefield_null(FLD))
#define set_nullfield_int2(FLD, VAL)		((VAL) != -1 ? set_tuplefield_int2(FLD, (VAL)) : set_tuplefield_null(FLD))
#define set_nullfield_int4(FLD, VAL)		((VAL) != -1 ? set_tuplefield_int4(FLD, (VAL)) : set_tuplefield_null(FLD))

void		set_tuplefield_null(TupleField *tuple_field);
void		set_tuplefield_string(TupleField *tuple_field, const char *string);
void		set_tuplefield_int2(TupleField *tuple_field, Int2 value);
void		set_tuplefield_int4(TupleField *tuple_field, Int4 value);
int	ClearCachedRows(TupleField *tuple, Int4 num_fields, int num_rows);
int	ReplaceCachedRows(TupleField *otuple, const TupleField *ituple, Int4 num_fields, int num_rows);

#endif
