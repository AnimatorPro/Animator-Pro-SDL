/*****************************************************************************
 *
 * struct.c - Keeps track of struct tags and member names/offsets/types.
 *
 *	08/25/90	(Ian)
 *				Changed all occurances of freemem() and gentle_freemem() to
 *				po_freemem() and poc_gentle_freemem().
 *	09/18/90	(Ian)
 *				Added po_move_sifs_to_parent() function to fix handling of
 *				nested structures.	Added support for unions.
 *	09/20/90	(Ian)
 *				Implemented the ANSI special rule for structure scoping: a
 *				definition of the form "struct tagname;" will build a new
 *				incomplete definition at the local scope even if the tag
 *				already exists at an outer scope.
 *				Also, fixed a 'do forever' loop bug in pf_to_sif() that
 *				would occur when struct tags were defined in nested struct
 *				defintions.
 *	10/01/90	(Ian)
 *				Added support for enum as a bastardized form of structure.
 *				Note that we are not 100% compliant with ANSI on enums: you
 *				should not be able to declare an incomplete enum defintion,
 *				and we allow it, since enums are treated like structs.	I've
 *				noticed that a couple other major compilers act this way.
 *	10/05/90	(Ian)
 *				Converted sprintf/po_say_fatal pairs to po_say_fatal w/formatting.
 *	10/07/90	(Ian)
 *				Removed NULL checks from calls to memory allocation.
 ****************************************************************************/

#include "poco_internal.h"
#include <string.h>
#include "fold.h"
#include "pocmemry.h"
#include "pocotype.h"
#include "statemen.h"

/*****************************************************************************
 * find structure in linked list, return pointer to struct_info.
 ****************************************************************************/
static Struct_info* in_sif_list(register Struct_info* l, char* name)
{
	while (l != NULL) {
		if (l->name[0] == *name) { /* quick-check 1st char before strcmp call */
			if (po_eqstrcmp(l->name, name) == 0) {
				return (l);
			}
		}
		l = l->next;
	}
	return (NULL);
}

/*****************************************************************************
 * find structure by searching from innermost scope outwards until found.
 ****************************************************************************/
static Struct_info* find_sif(Poco_cb* pcb, Poco_frame* pf, char* name)
{
	Struct_info* sif;
	(void)pcb;

	while (pf != NULL) {
		if ((sif = in_sif_list(pf->fsif, name)) != NULL) {
			return (sif);
		}
		pf = pf->next;
	}
	return (NULL);
}

/*****************************************************************************
 * alloc and init a new struct_info, attach it to the parent poco_frame.
 ****************************************************************************/
static Struct_info* new_sif(Poco_cb* pcb, Poco_frame* pf, char* name)
{
	Struct_info* new;

	new = po_memzalloc(pcb, sizeof(*new) + strlen(name) + 1);
	new->name = (char*)(new + 1);
	strcpy(new->name, name);
	new->unit_name = pcb->current_unit_name;
	new->next = pf->fsif;
	pf->fsif = new;
	return (new);
}

/*****************************************************************************
 * free struct_info, and symbols (member names) attached to it.
 ****************************************************************************/
static void free_sif(Struct_info* sif)
{
	if (sif != NULL) {
		po_free_symbol_list(&sif->elements);
		po_freemem(sif);
	}
}

/*****************************************************************************
 * free a linked list of stuct_infos.
 ****************************************************************************/
void po_free_sif_list(Struct_info** psif)
{
	Struct_info *sif, *next;

	next = *psif;
	while ((sif = next) != NULL) {
		next = next->next;
		free_sif(sif);
	}
	*psif = NULL;
}

/*****************************************************************************
 * move symbols from poco_frame to members list of struct_info.  calc sizes.
 ****************************************************************************/
static void pf_to_sif(Poco_cb* pcb, Poco_frame* pf, Struct_info* sif, SHORT ttype)
{
	Symbol *s, *link;
	long size;

	s = pf->symbols;
	while (s != NULL) {
		link = s->link;
		if (s->tok_type == PTOK_VAR) {
			s->next = s->link = sif->elements;
			sif->elements = s;
		} else {
			po_free_symbol(s);
		}
		s = link;
	}
	pf->symbols = NULL;
	s = sif->elements;
	while (s != NULL) {
		sif->el_count += 1;
		size = po_get_type_size(s->ti);
		if (size == 0) {
			po_say_fatal(pcb, "element %s in structure/union is of unknown size/type", s->name);
			PO_CHECK_ABORT_VOID(pcb);
		}
		if (ttype == TYPE_STRUCT) {
			s->symval.doff = sif->size;
			sif->size += size;
		} else {
			s->symval.doff = 0;
			if (sif->size < size) {
				sif->size = size;
			}
		}
		s = s->next;
	}
}

/*****************************************************************************
 * move any struct_infos from current poco_frame to parent (global/func) frame.
 * call this function only if:
 *	the current frame is an FTY_STRUCT type frame
 *	it is known that at least one sif is tied to the current frame
 ****************************************************************************/
void po_move_sifs_to_parent(Poco_cb* pcb)
{
	Struct_info* sifs; /* -> last sif in chain tied to current frame */
	Poco_frame* pf;    /* -> current frame */
	Poco_frame* rf;    /* -> parent frame */

	rf = pf = pcb->rframe;
	while (rf->frame_type == FTY_STRUCT) { /* find parent func/global frame */
		rf = rf->next;
	}

	sifs = pf->fsif;
	while (sifs->next != NULL) { /* find end of sif chain */
		sifs = sifs->next;
	}

	sifs->next = rf->fsif; /* concatenate sif chain of current 	*/
	rf->fsif = pf->fsif;   /* frame ahead of chain on parent frame 	*/

	pf->fsif = NULL; /* prevent chain from being free'd      */
}

/*****************************************************************************
 * process enum constant declarations until we see a closing brace.
 ****************************************************************************/
static void get_enum_block(Poco_cb* pcb, Poco_frame* pf)
{
	Exp_frame ef;
	Symbol* s;
	PToken_t ttype;
	int curval = 0;
	(void)pf;

	do {
		PO_CHECK_ABORT_VOID(pcb);
		po_need_token(pcb);
		ttype = pcb->t.toktype;
		switch (ttype) {
			case PTOK_UNDEF:
				s = pcb->curtoken->val.symbol;
				s->tok_type = PTOK_ENUMCONST;
				po_need_token(pcb);
				if (pcb->t.toktype != '=') {
					pushback_token(&pcb->t);
				} else {
					po_init_expframe(pcb, &ef);
					po_get_expression(pcb, &ef);
					curval = po_eval_const_expression(pcb, &ef);
					po_trash_expframe(pcb, &ef);
				}
				s->symval.i = curval++;
				ttype = po_need_comma_or_brace(pcb);
				break;
			case TOK_RBRACE:
				break;
			case PTOK_VAR:
				po_say_fatal(pcb, "enum constant name redefined");
				PO_CHECK_ABORT_VOID(pcb);
				break;
			default:
				po_expecting_got(pcb, "name of enum constant or }");
				break;
		}
	} while (ttype != TOK_RBRACE);
}

/*****************************************************************************
 * parse a struct/union statement ('struct' has been seen, this handles it).
 ****************************************************************************/
Struct_info* po_get_struct(Poco_cb* pcb, Poco_frame* pf, SHORT struct_union_ttype)
{
	Struct_info* sif = NULL;
	SHORT ttype;
	char* name;

	lookup_token(pcb);
	ttype = pcb->t.toktype;

	switch (ttype) {
		case TOK_LBRACE:

			sif = new_sif(pcb, pf, "");
			pushback_token(&pcb->t);
			break;

		case PTOK_UNDEF:
		case PTOK_VAR:
		case PTOK_LABEL:

			name = pcb->curtoken->val.symbol->name;
			sif = find_sif(pcb, pf, name);
			if (sif != NULL)                           /* Handle strange ANSI	*/
			{                                          /* rule:  if struct tag */
				lookup_token(pcb);                     /* exists at another	*/
				if (pcb->t.toktype == ';')             /* scope, and this def  */
				{                                      /* is 'struct name;',   */
					sif = in_sif_list(pf->fsif, name); /* then we build a new	*/
				} /* incomplete definition*/
				pushback_token(&pcb->t); /* at the current scope.*/
			}
			if (sif == NULL) {
				sif = new_sif(pcb, pf, name);
			}
			break;

		default:
			po_say_fatal(pcb, "malformed name for struct/union/enum...");
			PO_CHECK_ABORT(pcb, NULL);
			goto ERROR;
	}

	lookup_token(pcb);

	if (pcb->t.toktype == TOK_LBRACE) {
		if (sif->size != 0) {
			po_say_fatal(pcb, "struct/union/enum tag redefined");
			PO_CHECK_ABORT(pcb, NULL);
			goto ERROR;
		}
		if (struct_union_ttype == TYPE_ENUM) {
			get_enum_block(pcb, pf);
			sif->size = -1; /* used only to help catch dup tags */
		} else {
			po_new_frame(pcb, pf->scope + 1, sif->name, FTY_STRUCT);
			po_get_block(pcb, pcb->rframe);
			pf_to_sif(pcb, pcb->rframe, sif, struct_union_ttype);
			if (pcb->rframe->fsif != NULL) {
				po_move_sifs_to_parent(pcb);
			}
			po_old_frame(pcb);
		}
		sif->type = struct_union_ttype;
	} else {
		pushback_token(&pcb->t);
	}

	return (sif);

ERROR:
	return (NULL);
}

/*****************************************************************************
 * cross-unit agreement between struct/union/enum tags.
 *
 * Every translation unit mints its own Struct_info for each tag it sees, and
 * po_compile_source hands the finished list to pcb->run.struct_infos when the
 * unit closes.  Two units that give one tag two different layouts used to go
 * unnoticed: a struct's identity downstream is its index in that accumulated
 * table, so each unit silently resolved its own entry and the layouts
 * diverged.  The routines below compare a closing unit's tags against the
 * tags already accumulated, and report a disagreement.
 *
 * They only report.  Matching tags keep their separate entries, because that
 * index *is* the identity: collapsing two equal entries would renumber the
 * table and change what every encoded type reference means.
 ****************************************************************************/

/* Depth bound for the structural walk of anonymous tags.  Named tags stop the
 * recursion at their name, so this only applies to nesting of unnamed ones. */
#define MAX_TAG_COMPARE_DEPTH 16

static bool sifs_agree(const Struct_info* a, const Struct_info* b, int depth);

/*****************************************************************************
 * do two references to a tag from inside a member's type agree?
 *
 * A named tag is compared by name and kind and not walked into.  That is what
 * makes a self-referential tag terminate - 'struct node { struct node *next; }'
 * would otherwise walk forever - and it is also the right answer: the
 * referenced tag is itself in the unit's list, so its own members get compared
 * on their own pass.  An anonymous tag has no name to compare and cannot refer
 * to itself, so it is walked structurally instead, under a depth bound.
 ****************************************************************************/
static bool sif_refs_agree(const Struct_info* a, const Struct_info* b, int depth)
{
	if (a == b) {
		return (true);
	}
	if (a == NULL || b == NULL) {
		return (false);
	}
	if (a->type != b->type) {
		return (false);
	}
	if (a->name[0] != '\0' || b->name[0] != '\0') {
		return (po_eqstrcmp(a->name, b->name) == 0);
	}
	if (depth >= MAX_TAG_COMPARE_DEPTH) {
		return (true);
	}
	return (sifs_agree(a, b, depth + 1));
}

/*****************************************************************************
 * do two member types describe the same storage?
 ****************************************************************************/
static bool types_agree(const Type_info* a, const Type_info* b, int depth)
{
	int i;

	if (a == b) {
		return (true);
	}
	if (a == NULL || b == NULL) {
		return (false);
	}
	if (a->ido_type != b->ido_type || a->comp_count != b->comp_count) {
		return (false);
	}
	for (i = 0; i < a->comp_count; ++i) {
		if (a->comp[i] != b->comp[i]) {
			return (false);
		}
		switch (a->comp[i]) {
			case TYPE_STRUCT:
				if (!sif_refs_agree(a->sdims[i].pt, b->sdims[i].pt, depth)) {
					return (false);
				}
				break;
			case TYPE_ARRAY:
				if (a->sdims[i].l != b->sdims[i].l) {
					return (false);
				}
				break;
			default:
				/* TYPE_FUNCTION parks a Func_frame in sdims; a function
				 * pointer member occupies the same storage whatever it points
				 * at, so the signature is left out of a layout comparison. */
				break;
		}
	}
	return (true);
}

/*****************************************************************************
 * do two tags of the same name describe the same layout?
 *
 * Compared member by member rather than by size, since '{int x; int y;}' and
 * '{float a; float b;}' are both eight bytes and share nothing else.  Member
 * *names* are deliberately not compared: two units spelling one field 'x' and
 * 'first' still agree on layout.  The opposite choice is defensible - C's own
 * compatible-type rule does require the names to match - but what is at stake
 * here is the layout every consumer resolves by index, not source-level
 * compatibility.
 *
 * An enum records no members at all (its constants become symbols in the
 * enclosing frame, and its size is the -1 dup-tag sentinel), so two enums
 * sharing a tag always agree here.  Differing enumerator lists are therefore
 * not diagnosed; a tag used as an enum in one unit and a struct in another
 * still is, through the kind comparison.
 ****************************************************************************/
static bool sifs_agree(const Struct_info* a, const Struct_info* b, int depth)
{
	const Symbol* sa;
	const Symbol* sb;

	if (a->type != b->type || a->el_count != b->el_count) {
		return (false);
	}
	sa = a->elements;
	sb = b->elements;
	while (sa != NULL && sb != NULL) {
		if (!types_agree(sa->ti, sb->ti, depth)) {
			return (false);
		}
		sa = sa->next;
		sb = sb->next;
	}
	return (sa == NULL && sb == NULL);
}

/*****************************************************************************
 * a tag only describes a layout once a body has been parsed; sif->type stays
 * TYPE_END for a bare 'struct vec;' forward declaration, which says nothing
 * that could disagree with anything.
 ****************************************************************************/
static bool sif_is_defined(const Struct_info* sif)
{
	return (sif->type != TYPE_END);
}

/*****************************************************************************
 * report any tag this unit defines whose layout disagrees with the layout an
 * earlier unit gave the same tag.  Call at unit close, while the unit's own
 * list is still separate from the accumulated one.
 ****************************************************************************/
void po_check_struct_agreement(Poco_cb* pcb, Struct_info* unit_sifs, Struct_info* earlier_sifs)
{
	Struct_info* mine;
	Struct_info* theirs;

	for (mine = unit_sifs; mine != NULL; mine = mine->next) {
		/* An anonymous tag is unique to its definition, so it has no
		 * counterpart in another unit to disagree with. */
		if (mine->name[0] == '\0' || !sif_is_defined(mine)) {
			continue;
		}
		for (theirs = earlier_sifs; theirs != NULL; theirs = theirs->next) {
			if (theirs->name[0] == '\0' || !sif_is_defined(theirs)) {
				continue;
			}
			if (po_eqstrcmp(mine->name, theirs->name) != 0) {
				continue;
			}
			if (sifs_agree(mine, theirs, 0)) {
				continue;
			}
			po_say_fatal(pcb, "struct/union/enum tag '%s' is defined differently in '%s' and '%s'",
						 mine->name,
						 theirs->unit_name != NULL ? theirs->unit_name : "an earlier unit",
						 mine->unit_name != NULL ? mine->unit_name : "this unit");
			break; /* one diagnostic per tag, naming the first unit it met */
		}
	}
}
