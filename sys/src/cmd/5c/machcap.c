#include "gc.h"

int
machcap(Node *n)
{
	if(n == Z)
		return 0;	/* test */
	switch(n->op) {
	case OASADD:
	case OASSUB:
	case OASAND:
	case OASXOR:
	case OASOR:
	case OADD:
	case OSUB:
	case OAND:
	case OXOR:
	case OOR:
		if(typev[n->type->etype] && typev[n->left->type->etype] && typev[n->right->type->etype])
			return 1;
		break;

	case OMUL:
	case OLMUL:
		if(typev[n->type->etype] && typeil[n->left->type->etype] && typeil[n->right->type->etype]
		&& typeu[n->type->etype] == typeu[n->left->type->etype]
		&& typeu[n->type->etype] == typeu[n->right->type->etype])
			return 1;
		break;

	case OASASHL:
	case OASASHR:
	case OASLSHR:
	case OASHL:
	case OASHR:
	case OLSHR:
		if(typev[n->type->etype] && typev[n->left->type->etype] && n->right->op == OCONST)
			return 1;
		break;

	case OCAST:
		if(typeilp[n->type->etype] && typev[n->left->type->etype])
			return 1;
		if(typev[n->type->etype] && typeilp[n->left->type->etype])
			return 1;
		break;
	}
	return 0;
}
