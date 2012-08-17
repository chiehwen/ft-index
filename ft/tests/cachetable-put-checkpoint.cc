/* -*- mode: C++; c-basic-offset: 4; indent-tabs-mode: nil -*- */
// vim: ft=cpp:expandtab:ts=8:sw=4:softtabstop=4:
#ident "$Id$"
#ident "Copyright (c) 2007-2012 Tokutek Inc.  All rights reserved."
#include "includes.h"
#include "test.h"


//
// This test ensures that get_and_pin with dependent nodes works
// as intended with checkpoints, by having multiple threads changing
// values on elements in data, and ensure that checkpoints always get snapshots 
// such that the sum of all the elements in data are 0.
//

// The arrays

// must be power of 2 minus 1
#define NUM_ELEMENTS 127
// must be (NUM_ELEMENTS +1)/2 - 1
#define NUM_INTERNAL 63
#define NUM_MOVER_THREADS 4

int64_t data[NUM_ELEMENTS];
int64_t checkpointed_data[NUM_ELEMENTS];

uint32_t time_of_test;
bool run_test;

static void 
clone_callback(
    void* value_data, 
    void** cloned_value_data, 
    PAIR_ATTR* new_attr, 
    bool UU(for_checkpoint), 
    void* UU(write_extraargs)
    )
{
    new_attr->is_valid = false;
    int64_t* XMALLOC(data_val);
    *data_val = *(int64_t *)value_data;
    *cloned_value_data = data_val;
    *new_attr = make_pair_attr(8);
}

static void
flush (CACHEFILE f __attribute__((__unused__)),
       int UU(fd),
       CACHEKEY k  __attribute__((__unused__)),
       void *v     __attribute__((__unused__)),
       void** UU(dd),
       void *e     __attribute__((__unused__)),
       PAIR_ATTR s      __attribute__((__unused__)),
       PAIR_ATTR* new_size,
       bool write_me,
       bool keep_me,
       bool checkpoint_me,
        bool UU(is_clone)
       ) {
    int64_t val_to_write = *(int64_t *)v;
    size_t data_index = (size_t)k.b;
    if (write_me) {
        usleep(10);
        *new_size = make_pair_attr(8);
        data[data_index] = val_to_write;
        if (checkpoint_me) checkpointed_data[data_index] = val_to_write;
    }
    if (!keep_me) {
        toku_free(v);
    }
}

static int
fetch (CACHEFILE f        __attribute__((__unused__)),
       PAIR UU(p),
       int UU(fd),
       CACHEKEY k,
       uint32_t fullhash __attribute__((__unused__)),
       void **value,
       void** UU(dd),
       PAIR_ATTR *sizep,
       int  *dirtyp,
       void *extraargs    __attribute__((__unused__))
       ) {
    *dirtyp = 0;
    size_t data_index = (size_t)k.b;
    // assert that data_index is valid
    // if it is INT64_MAX, then that means
    // the block is not supposed to be in the cachetable
    assert(data[data_index] != INT64_MAX);
    
    int64_t* XMALLOC(data_val);
    usleep(10);
    *data_val = data[data_index];
    *value = data_val;
    *sizep = make_pair_attr(8);
    return 0;
}

static void *test_time(void *arg) {
    //
    // if num_Seconds is set to 0, run indefinitely
    //
    if (time_of_test != 0) {
        usleep(time_of_test*1000*1000);
        if (verbose) printf("should now end test\n");
        run_test = false;
    }
    if (verbose) printf("should be ending test now\n");
    return arg;
}

CACHETABLE ct;
CACHEFILE f1;

static void move_number_to_child(
    int parent, 
    int64_t* parent_val, 
    enum cachetable_dirty parent_dirty
    ) 
{
    int child = 0;
    int r;
    child = ((random() % 2) == 0) ? (2*parent + 1) : (2*parent + 2); 
    
    void* v1;
    long s1;
    CACHEKEY parent_key;
    parent_key.b = parent;
    uint32_t parent_fullhash = toku_cachetable_hash(f1, parent_key);

    
    CACHEKEY child_key;
    child_key.b = child;
    uint32_t child_fullhash = toku_cachetable_hash(f1, child_key);
    CACHETABLE_WRITE_CALLBACK wc = def_write_callback(NULL);
    wc.flush_callback = flush;
    wc.clone_callback = clone_callback;
    r = toku_cachetable_get_and_pin_with_dep_pairs(
        f1,
        child_key,
        child_fullhash,
        &v1,
        &s1,
        wc, fetch, def_pf_req_callback, def_pf_callback,
        true,
        NULL,
        1, //num_dependent_pairs
        &f1,
        &parent_key,
        &parent_fullhash,
        &parent_dirty
        );
    assert(r==0);
    
    int64_t* child_val = (int64_t *)v1;
    assert(child_val != parent_val); // sanity check that we are messing with different vals
    assert(*parent_val != INT64_MAX);
    assert(*child_val != INT64_MAX);
    usleep(10);
    (*parent_val)++;
    (*child_val)--;
    r = toku_test_cachetable_unpin(f1, parent_key, parent_fullhash, CACHETABLE_DIRTY, make_pair_attr(8));
    assert_zero(r);
    if (child < NUM_INTERNAL) {
        move_number_to_child(child, child_val, CACHETABLE_DIRTY);
    }
    else {
        r = toku_test_cachetable_unpin(f1, child_key, child_fullhash, CACHETABLE_DIRTY, make_pair_attr(8));
        assert_zero(r);
    }
}

static void *move_numbers(void *arg) {
    while (run_test) {
        int parent = 0;
        int r;
        void* v1;
        long s1;
        CACHEKEY parent_key;
        parent_key.b = parent;
        uint32_t parent_fullhash = toku_cachetable_hash(f1, parent_key);
        CACHETABLE_WRITE_CALLBACK wc = def_write_callback(NULL);
        wc.flush_callback = flush;
        wc.clone_callback = clone_callback;
        r = toku_cachetable_get_and_pin_with_dep_pairs(
            f1,
            parent_key,
            parent_fullhash,
            &v1,
            &s1,
            wc, fetch, def_pf_req_callback, def_pf_callback,
            true,
            NULL,
            0, //num_dependent_pairs
            NULL,
            NULL,
            NULL,
            NULL
            );
        assert(r==0);
        int64_t* parent_val = (int64_t *)v1;
        move_number_to_child(parent, parent_val, CACHETABLE_CLEAN);
    }
    return arg;
}

static void remove_data(CACHEKEY* cachekey, bool for_checkpoint, void* UU(extra)) {
    assert(cachekey->b < NUM_ELEMENTS);
    data[cachekey->b] = INT64_MAX;
    if (for_checkpoint) {
        checkpointed_data[cachekey->b] = INT64_MAX;
    }
}


static void get_data(CACHEKEY* cachekey, uint32_t* fullhash, void* extra) {
    int* CAST_FROM_VOIDP(key, extra);
    cachekey->b = *key;
    *fullhash = toku_cachetable_hash(f1, *cachekey);
    data[*key] = INT64_MAX - 1;
}

static void merge_and_split_child(
    int parent, 
    int64_t* parent_val, 
    enum cachetable_dirty parent_dirty
    ) 
{
    int child = 0;
    int other_child = 0;
    int r;
    bool even = (random() % 2) == 0;
    child = (even) ? (2*parent + 1) : (2*parent + 2); 
    other_child = (!even) ? (2*parent + 1) : (2*parent + 2);
    assert(child != other_child);
    
    void* v1;
    long s1;

    CACHEKEY parent_key;
    parent_key.b = parent;
    uint32_t parent_fullhash = toku_cachetable_hash(f1, parent_key);

    CACHEKEY child_key;
    child_key.b = child;
    uint32_t child_fullhash = toku_cachetable_hash(f1, child_key);
    enum cachetable_dirty child_dirty = CACHETABLE_CLEAN;
    CACHETABLE_WRITE_CALLBACK wc = def_write_callback(NULL);
    wc.flush_callback = flush;
    wc.clone_callback = clone_callback;
    r = toku_cachetable_get_and_pin_with_dep_pairs(
        f1,
        child_key,
        child_fullhash,
        &v1,
        &s1,
        wc, fetch, def_pf_req_callback, def_pf_callback,
        true,
        NULL,
        1, //num_dependent_pairs
        &f1,
        &parent_key,
        &parent_fullhash,
        &parent_dirty
        );
    assert(r==0);
    int64_t* child_val = (int64_t *)v1;
    
    CACHEKEY other_child_key;
    other_child_key.b = other_child;
    uint32_t other_child_fullhash = toku_cachetable_hash(f1, other_child_key);
    CACHEFILE cfs[2];
    cfs[0] = f1;
    cfs[1] = f1;
    CACHEKEY keys[2];
    keys[0] = parent_key;
    keys[1] = child_key;
    uint32_t hashes[2];
    hashes[0] = parent_fullhash;
    hashes[1] = child_fullhash;
    enum cachetable_dirty dirties[2];
    dirties[0] = parent_dirty;
    dirties[1] = child_dirty;
    
    r = toku_cachetable_get_and_pin_with_dep_pairs(
        f1,
        other_child_key,
        other_child_fullhash,
        &v1,
        &s1,
        wc, fetch, def_pf_req_callback, def_pf_callback,
        true,
        NULL,
        2, //num_dependent_pairs
        cfs,
        keys,
        hashes,
        dirties
        );
    assert(r==0);
    int64_t* other_child_val = (int64_t *)v1;
    assert(*parent_val != INT64_MAX);
    assert(*child_val != INT64_MAX);
    assert(*other_child_val != INT64_MAX);
    
    // lets get rid of other_child_val with a merge
    *child_val += *other_child_val;
    *other_child_val = INT64_MAX;        
    toku_test_cachetable_unpin_and_remove(f1, other_child_key, remove_data, NULL);
    dirties[1] = CACHETABLE_DIRTY;
    child_dirty = CACHETABLE_DIRTY;
    
    // now do a split
    CACHEKEY new_key;
    uint32_t new_fullhash;
    int64_t* XMALLOC(data_val);
    r = toku_cachetable_put_with_dep_pairs(
          f1,
          get_data,
          data_val,
          make_pair_attr(8),
          wc,
          &other_child,
          2, // number of dependent pairs that we may need to checkpoint
          cfs,
          keys,
          hashes,
          dirties,
          &new_key,
          &new_fullhash,
          put_callback_nop
          );
    assert(new_key.b == other_child);
    assert(new_fullhash == other_child_fullhash);
    *data_val = 5000;
    *child_val -= 5000;
    
    r = toku_test_cachetable_unpin(f1, parent_key, parent_fullhash, CACHETABLE_DIRTY, make_pair_attr(8));
    assert_zero(r);
    r = toku_test_cachetable_unpin(f1, other_child_key, other_child_fullhash, CACHETABLE_DIRTY, make_pair_attr(8));
    assert_zero(r);
    if (child < NUM_INTERNAL) {
        merge_and_split_child(child, child_val, CACHETABLE_DIRTY);
    }
    else {
        r = toku_test_cachetable_unpin(f1, child_key, child_fullhash, CACHETABLE_DIRTY, make_pair_attr(8));
        assert_zero(r);
    }
}

static void *merge_and_split(void *arg) {
    while (run_test) {
        int parent = 0;
        int r;        
        void* v1;
        long s1;
        CACHEKEY parent_key;
        parent_key.b = parent;
        uint32_t parent_fullhash = toku_cachetable_hash(f1, parent_key);
        CACHETABLE_WRITE_CALLBACK wc = def_write_callback(NULL);
        wc.flush_callback = flush;
        wc.clone_callback = clone_callback;
        r = toku_cachetable_get_and_pin_with_dep_pairs(
            f1,
            parent_key,
            parent_fullhash,
            &v1,
            &s1,
            wc, fetch, def_pf_req_callback, def_pf_callback,
            true,
            NULL,
            0, //num_dependent_pairs
            NULL,
            NULL,
            NULL,
            NULL
            );
        assert(r==0);
        int64_t* parent_val = (int64_t *)v1;
        merge_and_split_child(parent, parent_val, CACHETABLE_CLEAN);
    }
    return arg;
}

static int num_checkpoints = 0;
static void *checkpoints(void *arg) {
    // first verify that checkpointed_data is correct;
    while(run_test) {
        int64_t sum = 0;
        for (int i = 0; i < NUM_ELEMENTS; i++) {
            if (checkpointed_data[i] != INT64_MAX) {
                sum += checkpointed_data[i];
            }
        }
        assert (sum==0);
    
        //
        // now run a checkpoint
        //
        int r;
        CHECKPOINTER cp = toku_cachetable_get_checkpointer(ct);
        r = toku_cachetable_begin_checkpoint(cp, NULL); assert(r == 0);    
        r = toku_cachetable_end_checkpoint(
            cp, 
            NULL, 
            NULL,
            NULL
            );
        assert(r==0);
        assert (sum==0);
        for (int i = 0; i < NUM_ELEMENTS; i++) {
            if (checkpointed_data[i] != INT64_MAX) {
                sum += checkpointed_data[i];
            }
        }
        assert (sum==0);
        num_checkpoints++;
    }
    return arg;
}

static int
test_begin_checkpoint (
    LSN UU(checkpoint_lsn), 
    void* UU(header_v)) 
{
    memcpy(checkpointed_data, data, sizeof(int64_t)*NUM_ELEMENTS);
    return 0;
}

static int
dummy_int_checkpoint_userdata(CACHEFILE UU(cf), int UU(n), void* UU(extra)) {
    return 0;
}

static void sum_vals(void) {
    int64_t sum = 0;
    for (int i = 0; i < NUM_ELEMENTS; i++) {
        //printf("actual: i %d val %" PRId64 " \n", i, data[i]);
        if (data[i] != INT64_MAX) {
            sum += data[i];
        }
    }
    if (verbose) printf("actual sum %" PRId64 " \n", sum);
    assert(sum == 0);
    sum = 0;
    for (int i = 0; i < NUM_ELEMENTS; i++) {
        //printf("checkpointed: i %d val %" PRId64 " \n", i, checkpointed_data[i]);
        if (checkpointed_data[i] != INT64_MAX) {
            sum += checkpointed_data[i];
        }
    }
    if (verbose) printf("checkpointed sum %" PRId64 " \n", sum);
    assert(sum == 0);
}

static void
cachetable_test (void) {
    const int test_limit = NUM_ELEMENTS;

    //
    // let's set up the data
    //
    for (int64_t i = 0; i < NUM_ELEMENTS; i++) {
        data[i] = 0;
        checkpointed_data[i] = 0;
    }
    time_of_test = 60;

    int r;
    
    r = toku_create_cachetable(&ct, test_limit, ZERO_LSN, NULL_LOGGER); assert(r == 0);
    char fname1[] = __SRCFILE__ "test-put-checkpoint.dat";
    unlink(fname1);
    r = toku_cachetable_openf(&f1, ct, fname1, O_RDWR|O_CREAT, S_IRWXU|S_IRWXG|S_IRWXO); assert(r == 0);
    
    toku_cachefile_set_userdata(
        f1, 
        NULL, 
        NULL, 
        NULL, 
        NULL, 
        dummy_int_checkpoint_userdata, 
        test_begin_checkpoint, // called in begin_checkpoint
        dummy_int_checkpoint_userdata,
        NULL, 
        NULL
        );
    
    toku_pthread_t time_tid;
    toku_pthread_t checkpoint_tid;
    toku_pthread_t move_tid[NUM_MOVER_THREADS];
    toku_pthread_t merge_and_split_tid[NUM_MOVER_THREADS];
    run_test = true;

    for (int i = 0; i < NUM_MOVER_THREADS; i++) {
        r = toku_pthread_create(&move_tid[i], NULL, move_numbers, NULL); 
        assert_zero(r);
    }
    for (int i = 0; i < NUM_MOVER_THREADS; i++) {
        r = toku_pthread_create(&merge_and_split_tid[i], NULL, merge_and_split, NULL); 
        assert_zero(r);
    }
    r = toku_pthread_create(&checkpoint_tid, NULL, checkpoints, NULL); 
    assert_zero(r);    
    r = toku_pthread_create(&time_tid, NULL, test_time, NULL); 
    assert_zero(r);

    
    void *ret;
    r = toku_pthread_join(time_tid, &ret); 
    assert_zero(r);
    r = toku_pthread_join(checkpoint_tid, &ret); 
    assert_zero(r);
    for (int i = 0; i < NUM_MOVER_THREADS; i++) {
        r = toku_pthread_join(merge_and_split_tid[i], &ret); 
        assert_zero(r);
    }
    for (int i = 0; i < NUM_MOVER_THREADS; i++) {
        r = toku_pthread_join(move_tid[i], &ret); 
        assert_zero(r);
    }

    toku_cachetable_verify(ct);
    r = toku_cachefile_close(&f1, 0, false, ZERO_LSN); assert(r == 0);
    r = toku_cachetable_close(&ct); lazy_assert_zero(r);
    
    sum_vals();
    if (verbose) printf("num_checkpoints %d\n", num_checkpoints);
    
}

int
test_main(int argc, const char *argv[]) {
  default_parse_args(argc, argv);
  cachetable_test();
  return 0;
}