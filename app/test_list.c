/* Lightweight test suite for the intrusive doubly-linked list (list.h).
 *
 * Tests cover create, pushback, pop, remove, next/cnext, foreach, clear,
 * and destroy. Runs in M-mode so malloc is available for list internals.
 *
 * Each test prints [PASS] or [FAIL] and the overall summary.
 */

#include <linmo.h>

/* The list header – part of the kernel lib but usable from M-mode apps */
#include <lib/list.h>

/* ------------------------------------------------------------------ */
/*  Test helpers                                                      */
/* ------------------------------------------------------------------ */
static int tests_run, tests_passed, tests_failed;

#define TEST(cond, name)                 \
    do {                                 \
        tests_run++;                     \
        if (cond) {                      \
            tests_passed++;              \
            printf("[PASS] %s\n", name); \
        } else {                         \
            tests_failed++;              \
            printf("[FAIL] %s\n", name); \
        }                                \
    } while (0)

#define CHECK(cond)  \
    do {             \
        if (!(cond)) \
            return;  \
    } while (0)

/* ------------------------------------------------------------------ */
/*  Test: list_create / list_is_empty                                 */
/* ------------------------------------------------------------------ */
static void test_create_empty(void)
{
    list_t *l = list_create();
    TEST(l != NULL, "list_create returns non-NULL");
    TEST(list_is_empty(l), "fresh list is empty");
    TEST(l->head != NULL, "head sentinel exists");
    TEST(l->tail != NULL, "tail sentinel exists");
    TEST(l->head->next == l->tail, "head->next == tail initially");
    TEST(l->tail->prev == l->head, "tail->prev == head initially");
    TEST(l->length == 0U, "length == 0");

    /* Validate head/tail sentinel prev pointers */
    TEST(l->head->prev == NULL, "head->prev == NULL");
    TEST(l->tail->next == NULL, "tail->next == NULL");

    list_destroy(l);
}

/* ------------------------------------------------------------------ */
/*  Test: list_pushback, length, pop                                  */
/* ------------------------------------------------------------------ */
static void test_pushback_pop(void)
{
    int data[] = {10, 20, 30};
    list_t *l = list_create();
    CHECK(l);

    list_node_t *n1 = list_pushback(l, &data[0]);
    TEST(n1 != NULL, "pushback #1 ok");
    TEST(l->length == 1U, "length == 1 after one push");
    TEST(!list_is_empty(l), "non-empty after push");

    list_node_t *n2 = list_pushback(l, &data[1]);
    n2 = n2; /* suppress unused */
    TEST(l->length == 2U, "length == 2 after two pushes");

    list_node_t *n3 = list_pushback(l, &data[2]);
    n3 = n3;
    TEST(l->length == 3U, "length == 3 after three pushes");

    /* Verify sentinel connectivity */
    TEST(l->head->next == n1, "head->next == first node");
    TEST(l->tail->prev == n3, "tail->prev == last node");
    TEST(n1->prev == l->head, "first node->prev == head");
    TEST(n3->next == l->tail, "last node->next == tail");

    /* Pop all */
    void *d;
    d = list_pop(l);
    TEST(d == &data[0], "pop returns data[0]");
    d = list_pop(l);
    TEST(d == &data[1], "pop returns data[1]");
    d = list_pop(l);
    TEST(d == &data[2], "pop returns data[2]");
    TEST(list_is_empty(l), "list empty after three pops");
    TEST(l->length == 0U, "length == 0 after pops");
    TEST(l->head->next == l->tail, "head->next == tail after pops");
    TEST(l->tail->prev == l->head, "tail->prev == head after pops");

    list_destroy(l);
}

/* ------------------------------------------------------------------ */
/*  Test: list_remove by node pointer (O(1) via prev)                 */
/* ------------------------------------------------------------------ */
static void test_remove_node(void)
{
    int a = 1, b = 2, c = 3;
    list_t *l = list_create();
    CHECK(l);

    list_node_t *n1 = list_pushback(l, &a);
    list_node_t *n2 = list_pushback(l, &b);
    list_node_t *n3 = list_pushback(l, &c);
    CHECK(n1 && n2 && n3);

    /* Remove middle node via pointer */
    TEST(list_remove(l, n2) == &b, "remove middle returns data");
    TEST(l->length == 2U, "length == 2 after remove middle");
    TEST(n1->next == n3, "first->next points to last");
    TEST(n3->prev == n1, "last->prev points to first");

    /* Remove first node */
    TEST(list_remove(l, n1) == &a, "remove first returns data");
    TEST(l->length == 1U, "length == 1 after remove first");
    TEST(l->head->next == n3, "head->next == remaining node");
    TEST(n3->prev == l->head, "remaining->prev == head");

    /* Remove last node */
    TEST(list_remove(l, n3) == &c, "remove last returns data");
    TEST(list_is_empty(l), "empty after remove all");

    /* Remove from empty list */
    TEST(list_remove(l, NULL) == NULL, "remove NULL returns NULL");

    list_destroy(l);
}

/* ------------------------------------------------------------------ */
/*  Test: list_remove sentinel guard                                  */
/* ------------------------------------------------------------------ */
static void test_remove_sentinel(void)
{
    int x = 42;
    list_t *l = list_create();
    CHECK(l);

    list_pushback(l, &x);
    TEST(list_remove(l, l->head) == NULL, "remove head sentinel fails");
    TEST(list_remove(l, l->tail) == NULL, "remove tail sentinel fails");
    TEST(l->length == 1U, "list intact after sentinel removal");

    list_destroy(l);
}

/* ------------------------------------------------------------------ */
/*  Test: list_next / list_cnext                                      */
/* ------------------------------------------------------------------ */
static void test_next_cnext(void)
{
    int v[] = {100, 200};
    list_t *l = list_create();
    CHECK(l);

    TEST(list_next(l->head) == l->tail, "next(head) == tail on empty");
    TEST(list_next(NULL) == NULL, "next(NULL) == NULL");

    list_pushback(l, &v[0]);
    list_pushback(l, &v[1]);

    list_node_t *first = l->head->next;
    TEST(list_next(first) != l->tail, "next(first) is not tail");
    TEST(list_next(first)->data == &v[1], "next(first) == second node");

    /* cnext circular wrap */
    list_node_t *last = l->tail->prev;
    TEST(list_cnext(l, last) == first, "cnext(last) wraps to first");

    list_destroy(l);
}

/* ------------------------------------------------------------------ */
/*  Test: list_foreach (callback visits every node)                   */
/* ------------------------------------------------------------------ */
static int visit_count;
static list_node_t *count_nodes(list_node_t *n, void *arg)
{
    (void) arg;
    visit_count++;
    /* verify prev/next consistency */
    if (n->prev && n->prev->next != n)
        return n; /* corruption */
    if (n->next && n->next->prev != n)
        return n;
    return NULL;
}

static void test_foreach(void)
{
    int v[] = {1, 2, 3, 4};
    list_t *l = list_create();
    CHECK(l);

    /* empty foreach is safe */
    visit_count = 0;
    list_foreach(l, count_nodes, NULL);
    TEST(visit_count == 0, "foreach on empty visits 0 nodes");

    for (int i = 0; i < 4; i++)
        list_pushback(l, &v[i]);

    visit_count = 0;
    list_foreach(l, count_nodes, NULL);
    TEST(visit_count == 4, "foreach visits 4 nodes");

    list_clear(l);
    visit_count = 0;
    list_foreach(l, count_nodes, NULL);
    TEST(visit_count == 0, "foreach on cleared visits 0");

    list_destroy(l);
}

/* ------------------------------------------------------------------ */
/*  Test: list_clear / list_destroy (no-crash only)                   */
/* ------------------------------------------------------------------ */
static void test_clear_destroy(void)
{
    int v[] = {1, 2};
    list_t *l = list_create();
    CHECK(l);

    list_pushback(l, &v[0]);
    list_pushback(l, &v[1]);

    list_clear(l);
    TEST(list_is_empty(l), "list empty after clear");
    TEST(l->length == 0U, "length == 0 after clear");
    /* Validate sentinels are still wired correctly */
    TEST(l->head->next == l->tail, "head->next == tail after clear");
    TEST(l->tail->prev == l->head, "tail->prev == head after clear");

    /* second clear on empty list should be harmless */
    list_clear(l);
    TEST(list_is_empty(l), "second clear still ok");

    /* destroy must not crash */
    list_destroy(l);

    /* destroy on NULL must not crash */
    list_destroy(NULL);
    TEST(1, "list_destroy(NULL) does not crash");
}

/* ------------------------------------------------------------------ */
/*  Main                                                              */
/* ------------------------------------------------------------------ */
int32_t app_main(void)
{
    printf("\n=== List Unit Tests ===\n\n");

    test_create_empty();
    test_pushback_pop();
    test_remove_node();
    test_remove_sentinel();
    test_next_cnext();
    test_foreach();
    test_clear_destroy();

    printf("\n=== Summary ===\n");
    printf("Tests run:    %d\n", tests_run);
    printf("Passed:       %d\n", tests_passed);
    printf("Failed:       %d\n", tests_failed);

    if (tests_failed == 0)
        printf("\n[SUCCESS] All list tests passed!\n");
    else
        printf("\n[FAILURE] %d test(s) failed!\n", tests_failed);

    /* Signal QEMU shutdown (virt machine poweroff) */
    *(volatile uint32_t *) 0x100000U = 0x5555U;
    while (1)
        ;
    return 0;
}
