#ifndef SZPONTOS_KERNEL_LIST_H
#define SZPONTOS_KERNEL_LIST_H

#include <kernel/types.h>

typedef struct list_node {
    struct list_node *prev;
    struct list_node *next;
} list_node_t;

#define LIST_HEAD_INIT(name) {&(name), &(name)}
#define LIST_HEAD(name) list_node_t name = LIST_HEAD_INIT(name)

static inline void list_init(list_node_t *list) {
    list->prev = list;
    list->next = list;
}

static inline bool list_is_empty(const list_node_t *list) {
    return list->next == list;
}

static inline void list_add_after(list_node_t *pos, list_node_t *new_node) {
    new_node->prev = pos;
    new_node->next = pos->next;
    pos->next->prev = new_node;
    pos->next = new_node;
}

static inline void list_add_before(list_node_t *pos, list_node_t *new_node) {
    new_node->next = pos;
    new_node->prev = pos->prev;
    pos->prev->next = new_node;
    pos->prev = new_node;
}

static inline void list_add_head(list_node_t *list, list_node_t *new_node) {
    list_add_after(list, new_node);
}

static inline void list_add_tail(list_node_t *list, list_node_t *new_node) {
    list_add_before(list, new_node);
}

static inline void list_remove(list_node_t *node) {
    if (!node || !node->prev || !node->next) {
        if (node) {
            node->prev = node;
            node->next = node;
        }
        return;
    }
    node->prev->next = node->next;
    node->next->prev = node->prev;
    node->prev = node;
    node->next = node;
}

#define container_of(ptr, type, member) ((type *)((char *)(ptr) - offsetof(type, member)))

#define list_entry(ptr, type, member) container_of(ptr, type, member)

#define list_first_entry(ptr, type, member) list_entry((ptr)->next, type, member)

#define list_for_each(pos, head) for (pos = (head)->next; pos != (head); pos = pos->next)

#define list_for_each_safe(pos, n, head) for (pos = (head)->next, n = pos->next; pos != (head); pos = n, n = pos->next)

#endif /* SZPONTOS_KERNEL_LIST_H */
