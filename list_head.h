#ifndef _LIST_HEAD_H
#define _LIST_HEAD_H

struct list_head{
	struct list_head * prev;
	struct list_head * next;
};
#define LH_SZ	sizeof(struct list_head)

static inline void list_head_init(struct list_head *lh)
{
	lh->prev = lh;
	lh->next = lh;
}

static inline void __list_add(struct list_head * n,
		struct list_head * prev,
		struct list_head * next)
{
	n->prev = prev;
	n->next = next;
	next->prev = n;
	prev->next = n;
}

static inline int list_empty(const struct list_head * head)
{
	return (head->next == head && head->prev == head);
}

static inline void list_add(struct list_head * n,struct list_head * head)
{
	__list_add(n,head,head->next);
}

static inline void list_add_tail(struct list_head * n,struct list_head * head)
{
	__list_add(n,head->prev,head);
}

static inline void __list_del(struct list_head * prev,struct list_head * next)
{
	prev->next = next;
	next->prev = prev;
}

static inline void list_del(struct list_head * p)
{
	__list_del(p->prev,p->next);
}

static inline void list_replace(struct list_head * n,struct list_head * o)
{
	n->next = o->next;
	n->next->prev = n;
	n->prev = o->prev;
	n->prev->next = n;
}

#define for_each_list_head_between(p,s,e)	for(p=(s)->next;p!=(e);p=p->next)
#define for_each_lhe(p,head)	for_each_list_head_between(p,head,head)

#endif
