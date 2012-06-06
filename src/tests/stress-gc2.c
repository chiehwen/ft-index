/* -*- mode: C; c-basic-offset: 4; indent-tabs-mode: nil -*- */
// vim: expandtab:ts=8:sw=4:softtabstop=4:
#ident "Copyright (c) 2007 Tokutek Inc.  All rights reserved."
#ident "$Id: test_stress1.c 39258 2012-01-27 13:51:58Z zardosht $"
#include "test.h"

#include <stdio.h>
#include <stdlib.h>

#include <toku_pthread.h>
#include <unistd.h>
#include <memory.h>
#include <sys/stat.h>
#include <db.h>

#include "threaded_stress_test_helpers.h"

// The intent of this test is to measure create and abort transactions
// with garbage collection verification on

static int random_sleep(DB_TXN* UU(txn), ARG UU(arg), void* UU(operation_extra)) {
    usleep(random()%2000);
    return 0;
}


static void
stress_table(DB_ENV* env, DB** dbp, struct cli_args *cli_args) {
    int n = cli_args->num_elements;
    if (verbose) printf("starting creation of pthreads\n");
    const int num_threads = cli_args->num_ptquery_threads;
    struct arg myargs[num_threads];
    for (int i = 0; i < num_threads; i++) {
        arg_init(&myargs[i], n, dbp, env, cli_args);
    }
    for (int i = 0; i < num_threads; i++) {
        myargs[i].operation = random_sleep;
    }
    run_workers(myargs, num_threads, cli_args->time_of_test, false, cli_args);
}

int
test_main(int UU(argc), char *const UU(argv[])) {
    struct cli_args args = get_default_args_for_perf();
    db_env_set_mvcc_garbage_collection_verification(1);
    args.time_of_test = 60;
    args.num_ptquery_threads = 12;
    stress_test_main(&args);
    return 0;
}