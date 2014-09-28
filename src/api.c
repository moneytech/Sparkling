/*
 * api.c
 * Sparkling, a lightweight C-style scripting language
 *
 * Created by Árpád Goretity on 02/05/2013
 * Licensed under the 2-clause BSD License
 *
 * Public parts of the Sparkling API
 */

#include <stdio.h>
#include <stdlib.h>
#include <limits.h>
#include <float.h>
#include <assert.h>

#include "api.h"
#include "parser.h"
#include "compiler.h"
#include "vm.h"
#include "private.h"
#include "array.h"
#include "func.h"


/*
 * Object API
 */

int spn_object_equal(void *lp, void *rp)
{
	SpnObject *lhs = lp, *rhs = rp;

	if (lhs->isa != rhs->isa) {
		return 0;
	}

	if (lhs->isa->equal != NULL) {
		return lhs->isa->equal(lhs, rhs);
	} else {
		return lhs == rhs;
	}
}

int spn_object_cmp(void *lp, void *rp)
{
	SpnObject *lhs = lp, *rhs = rp;

	assert(lhs->isa == rhs->isa);
	assert(lhs->isa->compare != NULL);

	return lhs->isa->compare(lhs, rhs);
}

void *spn_object_new(const SpnClass *isa)
{
	SpnObject *obj = spn_malloc(isa->instsz);

	obj->isa = isa;
	obj->refcnt = 1;

	return obj;
}

void spn_object_retain(void *o)
{
	SpnObject *obj = o;
	obj->refcnt++;
}

void spn_object_release(void *o)
{
	SpnObject *obj = o;
	if (--obj->refcnt == 0) {
		if (obj->isa->destructor) {
			obj->isa->destructor(obj);
		}

		free(obj);
	}
}

/*
 * Value API
 */

SpnValue spn_makenil(void)
{
	SpnValue ret = { SPN_TYPE_NIL, { 0 } };
	return ret;
}

SpnValue spn_makebool(int b)
{
	SpnValue ret;
	ret.type = SPN_TYPE_BOOL;
	ret.v.b = b;
	return ret;
}

SpnValue spn_makeint(long i)
{
	SpnValue ret;
	ret.type = SPN_TYPE_INT;
	ret.v.i = i;
	return ret;
}

SpnValue spn_makefloat(double f)
{
	SpnValue ret;
	ret.type = SPN_TYPE_FLOAT;
	ret.v.f = f;
	return ret;
}

SpnValue spn_makeweakuserinfo(void *p)
{
	SpnValue ret;
	ret.type = SPN_TYPE_WEAKUSERINFO;
	ret.v.p = p;
	return ret;
}

SpnValue spn_makestrguserinfo(void *o)
{
	SpnValue ret;
	ret.type = SPN_TYPE_STRGUSERINFO;
	ret.v.o = o;
	return ret;
}

void spn_value_retain(const SpnValue *val)
{
	if (isobject(val)) {
		assert(isstring(val) || isarray(val)
		    || isfunc(val)   || isuserinfo(val));

		spn_object_retain(objvalue(val));
	}
}

void spn_value_release(const SpnValue *val)
{
	if (isobject(val)) {
		assert(isstring(val) || isarray(val)
		    || isfunc(val)   || isuserinfo(val));

		spn_object_release(objvalue(val));
	}
}


static int numeric_equal(const SpnValue *lhs, const SpnValue *rhs)
{
	assert(isnum(lhs) && isnum(rhs));

	if (isfloat(lhs)) {
		return isfloat(rhs) ? floatvalue(lhs) == floatvalue(rhs)
		                    : floatvalue(lhs) == intvalue(rhs);
	} else {
		return isfloat(rhs) ? intvalue(lhs) == floatvalue(rhs)
		                    : intvalue(lhs) == intvalue(rhs);
	}
}


int spn_value_equal(const SpnValue *lhs, const SpnValue *rhs)
{
	/* first, make sure that we compare values of the same type
	 * (values of different types cannot possibly be equal)
	 */
	if (valtype(lhs) != valtype(rhs)) {
		return 0;
	}

	switch (valtype(lhs)) {
	case SPN_TTAG_NIL:    { return 1; /* nil can only be nil */	   }
	case SPN_TTAG_BOOL:   { return boolvalue(lhs) == boolvalue(rhs); }
	case SPN_TTAG_NUMBER: { return numeric_equal(lhs, rhs);	   }

	case SPN_TTAG_STRING:
	case SPN_TTAG_ARRAY:
	case SPN_TTAG_FUNC:	{
		return spn_object_equal(objvalue(lhs), objvalue(rhs));
	}

	case SPN_TTAG_USERINFO:	{
		if (isobject(lhs) != isobject(rhs)) {
			return 0;
		}

		if (isobject(lhs)) {
			SpnObject *lo = objvalue(lhs);
			SpnObject *ro = objvalue(rhs);

			if (lo->isa != ro->isa) {
				return 0;
			}

			return spn_object_equal(lo, ro);
		} else {
			return ptrvalue(lhs) == ptrvalue(rhs);
		}
	}
	default:
		SHANT_BE_REACHED();
	}

	return 0;
}

int spn_value_noteq(const SpnValue *lhs, const SpnValue *rhs)
{
	return !spn_value_equal(lhs, rhs);
}


/* Functions for performing ordered comparison */

static int numeric_compare(const SpnValue *lhs, const SpnValue *rhs)
{
	assert(isnum(lhs) && isnum(rhs));

	if (isfloat(lhs)) {
		if (isfloat(rhs)) {
			return floatvalue(lhs) < floatvalue(rhs) ? -1
			     : floatvalue(lhs) > floatvalue(rhs) ? +1
			     :                                      0;
		} else {
			return floatvalue(lhs) < intvalue(rhs) ? -1
			     : floatvalue(lhs) > intvalue(rhs) ? +1
			     :                                    0;
		}
	} else {
		if (isfloat(rhs)) {
			return intvalue(lhs) < floatvalue(rhs) ? -1
			     : intvalue(lhs) > floatvalue(rhs) ? +1
			     :                                    0;
		} else {
			return intvalue(lhs) < intvalue(rhs) ? -1
			     : intvalue(lhs) > intvalue(rhs) ? +1
			     :                                  0;
		}
	}
}

int spn_value_compare(const SpnValue *lhs, const SpnValue *rhs)
{
	if (isnum(lhs) && isnum(rhs)) {
		return numeric_compare(lhs, rhs);
	}

	/* else assume comparable objects */
	return spn_object_cmp(objvalue(lhs), objvalue(rhs));
}

int spn_values_comparable(const SpnValue *lhs, const SpnValue *rhs)
{
	if (isnum(lhs) && isnum(rhs)) {
		return 1;
	}

	if (isobject(lhs) && isobject(rhs)) {
		SpnObject *ol = objvalue(lhs), *or = objvalue(rhs);
		if (ol->isa != or->isa) {
			return 0;
		}

		return ol->isa->compare != NULL;
	}

	return 0;
}

/* The hash function is the SMDB hash */
unsigned long spn_hash_bytes(const void *data, size_t n)
{
	unsigned long hash = 0;
	const unsigned char *p = data;

	while (n--) {
		hash *= 65599;
		hash += *p++;
	}

	return hash;
}

unsigned long spn_hash_value(const SpnValue *key)
{
	switch (valtype(key)) {
	case SPN_TTAG_NIL:	{ return 0;				}
	case SPN_TTAG_BOOL:	{ return boolvalue(key); /* 0 or 1 */	}
	case SPN_TTAG_NUMBER: {
		if (isfloat(key)) {
			double f = floatvalue(key);

			if (f == (long)(f)) {
				return (unsigned long)(f);
			} else {
				return spn_hash_bytes(&f, sizeof f);
			}
		}

		/* the hash value of an integer is itself */
		return intvalue(key);
	}
	case SPN_TTAG_STRING:
	case SPN_TTAG_ARRAY:
	case SPN_TTAG_FUNC: {
		SpnObject *obj = objvalue(key);
		unsigned long (*hashfn)(void *) = obj->isa->hashfn;
		return hashfn ? hashfn(obj) : (unsigned long)(obj);
	}
	case SPN_TTAG_USERINFO: {
		if (isobject(key)) {
			SpnObject *obj = objvalue(key);
			unsigned long (*hashfn)(void *) = obj->isa->hashfn;
			return hashfn ? hashfn(obj) : (unsigned long)(obj);
		}

		return (unsigned long)(ptrvalue(key));
	}
	default:
		SHANT_BE_REACHED();
	}

	return 0;
}

static void print_indent(int level)
{
	int i;
	for (i = 0; i < level; i++) {
		printf("    ");
	}
}

static void print_array(SpnArray *array, int level)
{
	size_t n = spn_array_count(array);
	SpnValue i_key, i_val;
	SpnIterator *it = spn_iter_new(array);

	printf("(\n");

	while (spn_iter_next(it, &i_key, &i_val) < n) {
		print_indent(level + 1);

		if (isarray(&i_key)) {
			print_array(arrayvalue(&i_key), level + 1);
		} else {
			spn_debug_print(&i_key);
		}

		printf(": ");

		if (isarray(&i_val)) {
			print_array(arrayvalue(&i_val), level + 1);
		} else {
			spn_debug_print(&i_val);
		}

		printf("\n");
	}

	print_indent(level);
	printf(")");

	spn_iter_free(it);
}

void spn_value_print(const SpnValue *val)
{
	switch (valtype(val)) {
	case SPN_TTAG_NIL: {
		fputs("nil", stdout);
		break;
	}
	case SPN_TTAG_BOOL: {
		fputs(boolvalue(val) ? "true" : "false", stdout);
		break;
	}
	case SPN_TTAG_NUMBER: {
		if (isfloat(val)) {
			printf("%.*g", DBL_DIG, floatvalue(val));
		} else {
			printf("%ld", intvalue(val));
		}

		break;
	}
	case SPN_TTAG_STRING: {
		SpnString *s = stringvalue(val);
		fputs(s->cstr, stdout);
		break;
	}
	case SPN_TTAG_ARRAY: {
		SpnArray *array = objvalue(val);
		print_array(array, 0);
		break;
	}
	case SPN_TTAG_FUNC: {
		SpnFunction *func = funcvalue(val);
		void *p;

		if (func->native) {
			p = (void *)(ptrdiff_t)(func->repr.fn);
		} else {
			p = func->repr.bc;
		}

		printf("<function %p>", p);
		break;
	}
	case SPN_TTAG_USERINFO: {
		void *ptr = isobject(val) ? objvalue(val) : ptrvalue(val);
		printf("<userinfo %p>", ptr);
		break;
	}
	default:
		SHANT_BE_REACHED();
		break;
	}
}

void spn_debug_print(const SpnValue *val)
{
	switch (valtype(val)) {
	case SPN_TTAG_STRING:
		/* TODO: do proper escaping */
		printf("\"");
		spn_value_print(val);
		printf("\"");
		break;
	case SPN_TTAG_ARRAY:
		printf("<array %p>", objvalue(val));
		break;
	default:
		spn_value_print(val);
		break;
	}
}

void spn_repl_print(const SpnValue *val)
{
	switch (valtype(val)) {
	case SPN_TTAG_STRING:
		spn_debug_print(val);
		break;
	default:
		spn_value_print(val);
		break;
	}
}

const char *spn_type_name(int type)
{
	/* XXX: black magic, relies on the order of enum members */
	static const char *const typenames[] = {
		"nil",
		"bool",
		"number",
		"string",
		"array",
		"function",
		"userinfo"
	};

	return typenames[typetag(type)];
}


/*
 * File access API
 */

static char *read_file2mem(const char *name, size_t *sz, int nulterm)
{
	long n;
	size_t len;
	char *buf;
	FILE *f;

	f = fopen(name, "rb");
	if (f == NULL) {
		return NULL;
	}

	if (fseek(f, 0, SEEK_END) < 0) {
		fclose(f);
		return NULL;
	}

	n = ftell(f);
	if (n < 0) {
		fclose(f);
		return NULL;
	}

	/* don't get confused by empty text files */
	if (n == 0 && nulterm) {
		fclose(f);

		buf = malloc(1);
		if (buf == NULL) {
			return NULL;
		}

		buf[0] = 0;
		return buf;
	}

	if (fseek(f, 0, SEEK_SET) < 0) {
		fclose(f);
		return NULL;
	}

	len = nulterm ? n + 1 : n;
	buf = malloc(len);
	if (buf == NULL) {
		fclose(f);
		return NULL;
	}

	if (fread(buf, n, 1, f) < 1) {
		fclose(f);
		free(buf);
		return NULL;
	}

	fclose(f);

	if (nulterm) {
		buf[n] = 0;
	}

	if (sz != NULL) {
		*sz = n;
	}

	return buf;
}


char *spn_read_text_file(const char *name)
{
	return read_file2mem(name, NULL, 1);
}

void *spn_read_binary_file(const char *name, size_t *sz)
{
	return read_file2mem(name, sz, 0);
}
