#include "pgtest/bufpage.h"
#include "pgtest/relation.h"
#include "pgtest/heap.h"
#include "pgtest/tupdesc.h"
#include "pgtest/buffer.h"
#include "pgtest/index.h"
#include "pgtest/btree.h"
#include "pgtest/fmgr.h"
#include "pgtest/catalog.h"
#include "pgtest/aggregate.h"

#include <storpu_interface.h>

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <storpu.h>
#include <storpu/file.h>

#define roundup(x, align) \
    (((x) % align == 0) ? (x) : (((x) + align) - ((x) % align)))

struct tablescan_state {
    TableScanDesc scan;
    SnapshotData snapshot;
    ScanKey scankey;
    unsigned int nscankey;
    void* buf;
    size_t buf_size;
    size_t total_count;
    bool finished;
};

static Datum scan_func_int(PG_FUNCTION_ARGS)
{
    spu_printf("Datum %d\n", (unsigned int)PG_GETARG_DATUM(0));
    return 1;
}

static Datum scan_func1(PG_FUNCTION_ARGS)
{
    char* str = text_to_cstring((text*)PG_GETARG_DATUM(0));
    spu_printf("Datum %s\n", str);
    free(str);

    return 1;
}

static Datum scan_func2(PG_FUNCTION_ARGS)
{
    spu_printf("Datum %d\n", VARSIZE_ANY_EXHDR(PG_GETARG_DATUM(0)));
    return 1;
}

Datum F_int4_gt(PG_FUNCTION_ARGS)
{
    Datum lhs = PG_GETARG_DATUM(0);
    Datum rhs = PG_GETARG_DATUM(1);

    return (int32_t)lhs > (int32_t)rhs;
}

static Datum scan_func_int4_leq(PG_FUNCTION_ARGS)
{
    Datum lhs = PG_GETARG_DATUM(0);
    Datum rhs = PG_GETARG_DATUM(1);

    return (int32_t)lhs <= (int32_t)rhs;
}

int pgtest_scan()
{
    RelationData rfp;
    RelationData rind;

    rel_open_relation(&rfp, REL_OID_TABLE_TPCH_LINEITEM);

    TableScanDesc scan;

    ScanKeyData skey[1];
    ScanKeyInit(&skey[0], 16, 0, scan_func1, (Datum)0);

    SnapshotData snapshot;

    scan = heap_beginscan(&rfp, &snapshot, 1, skey);
    heap_rescan(scan, NULL);

    for (int i = 0; i < 200; i++)
        heap_getnext(scan, ForwardScanDirection);

    heap_endscan(scan);

    return 0;
}

Relation storpu_open_relation(unsigned long arg)
{
    Relation rel;
    Oid relid = (Oid)arg;

    rel = (Relation)malloc(sizeof(RelationData));
    rel_open_relation(rel, relid);

    return rel;
}

void storpu_close_relation(unsigned long arg)
{
    Relation rel = (Relation)arg;
    free(rel);
}

struct tablescan_state* storpu_table_beginscan(unsigned long arg)
{
    struct storpu_table_beginscan_arg tbsa;
    struct storpu_scankey* sskey;
    struct tablescan_state* state;
    int i;

    spu_read(FD_SCRATCHPAD, &tbsa, sizeof(tbsa), arg);

    state = malloc(sizeof(*state));
    if (!state) return NULL;

    memset(state, 0, sizeof(*state));

    state->nscankey = tbsa.num_scankeys;

    if (state->nscankey > 0) {
        sskey = malloc(tbsa.num_scankeys * sizeof(struct storpu_scankey));
        spu_read(FD_SCRATCHPAD, sskey,
                 tbsa.num_scankeys * sizeof(struct storpu_scankey),
                 arg + sizeof(tbsa));

        state->scankey = malloc(sizeof(ScanKeyData) * state->nscankey);

        for (i = 0; i < state->nscankey; i++) {
            struct storpu_scankey* key = &sskey[i];
            const FmgrBuiltin* builtin = fmgr_isbuiltin(key->func);
            Datum arg = key->arg;

            if (key->flags & SSK_REF_ARG) {
                arg = (Datum)malloc(key->arglen);
                spu_read(FD_SCRATCHPAD, (void*)arg, key->arglen, key->arg);
            }

            ScanKeyInit(&state->scankey[i], key->attr_num, key->strategy,
                        builtin->func, arg);
            state->scankey[i].sk_flags = key->flags;
        }

        free(sskey);
    } else {
        state->scankey = NULL;
    }

    state->scan = heap_beginscan(tbsa.relation, &state->snapshot,
                                 state->nscankey, state->scankey);
    heap_rescan(state->scan, NULL);

    state->finished = false;
    state->total_count = 0;

    return state;
}

size_t storpu_table_getnext(unsigned long arg)
{
    struct storpu_table_getnext_arg tga;
    struct tablescan_state* state;

    spu_read(FD_SCRATCHPAD, &tga, sizeof(tga), arg);
    state = (struct tablescan_state*)tga.scan_state;

    if ((state->buf == NULL) || (state->buf_size != tga.buf_size)) {
        if (state->buf) munmap(state->buf, state->buf_size);

        state->buf = mmap(
            NULL, tga.buf_size, PROT_READ | PROT_WRITE,
            MAP_PRIVATE | MAP_ANONYMOUS | MAP_POPULATE | MAP_CONTIG, -1, 0);
        state->buf_size = tga.buf_size;
    }

    if (state->finished) return 0;

    size_t count = 0;

    while (true) {
        if (state->total_count && ((state->total_count % 100000) == 0))
            spu_printf("Processed %lu tuples\n", state->total_count);

        HeapTuple htup = heap_getnext(state->scan, ForwardScanDirection);

        if (!htup) {
            state->finished = true;
            break;
        }

        if (count + 2 + htup->t_len > tga.buf_size) {
            heap_getnext(state->scan, BackwardScanDirection);
            break;
        }

        state->total_count++;

        *(uint16_t*)(state->buf + count) = htup->t_len;
        count += 2;
        memcpy(state->buf + count, htup->t_data, htup->t_len);
        count += htup->t_len;

        if (count >= tga.buf_size) break;
    }

    if (count > 0) {
        size_t copy_count = roundup(count, 64);

        if (copy_count > state->buf_size) copy_count = state->buf_size;
        spu_write(FD_HOST_MEM, state->buf, copy_count, tga.buf);
    }

    return count;
}

void storpu_table_endscan(unsigned long arg)
{
    struct tablescan_state* state = (struct tablescan_state*)arg;
    int i;

    heap_endscan(state->scan);

    for (i = 0; i < state->nscankey; i++) {
        if (state->scankey[i].sk_flags & SSK_REF_ARG)
            free((void*)state->scankey[i].sk_argument);
    }
    free(state->scankey);

    if (state->buf) munmap(state->buf, state->buf_size);
    free(state);
}

struct aggregate_state {
    AggState* agg;
    void* buf;
    size_t buf_size;
    TupleTableSlot* last_slot;
    bool finished;
};

struct aggregate_state* storpu_aggregate_init(unsigned long arg)
{
    struct storpu_agg_init_arg aia;
    struct storpu_aggdesc* aggdesc;
    struct aggregate_state* state;
    struct tablescan_state* scan;
    AggregateDesc desc;
    int i;

    spu_read(FD_SCRATCHPAD, &aia, sizeof(aia), arg);

    scan = (struct tablescan_state*)aia.scan_state;

    state = malloc(sizeof(*state));
    if (!state) return NULL;

    memset(state, 0, sizeof(*state));

    aggdesc = malloc(aia.num_aggs * sizeof(struct storpu_aggdesc));
    spu_read(FD_SCRATCHPAD, aggdesc,
             aia.num_aggs * sizeof(struct storpu_aggdesc), arg + sizeof(aia));

    desc = malloc(sizeof(AggregateDescData) * aia.num_aggs);

    for (i = 0; i < aia.num_aggs; i++) {
        desc[i].agg_id = aggdesc[i].aggid;
        desc[i].attnum = aggdesc[i].attnum;
    }

    state->agg = agg_init(scan->scan, desc, aia.num_aggs, aia.group_size);

    free(aggdesc);
    free(desc);

    return state;
}

size_t storpu_aggregate_getnext(unsigned long arg)
{
    struct storpu_agg_getnext_arg aga;
    struct aggregate_state* state;

    spu_read(FD_SCRATCHPAD, &aga, sizeof(aga), arg);
    state = (struct aggregate_state*)aga.agg_state;

    if ((state->buf == NULL) || (state->buf_size != aga.buf_size)) {
        if (state->buf) munmap(state->buf, state->buf_size);

        state->buf = mmap(
            NULL, aga.buf_size, PROT_READ | PROT_WRITE,
            MAP_PRIVATE | MAP_ANONYMOUS | MAP_POPULATE | MAP_CONTIG, -1, 0);
        state->buf_size = aga.buf_size;
    }

    if (state->finished) return 0;

    size_t count = 0;
    while (true) {
        int i;
        size_t slot_size;
        TupleTableSlot* slot;

        if (state->last_slot) {
            slot = state->last_slot;
            state->last_slot = NULL;
        } else
            slot = agg_getnext(state->agg);

        if (!slot) {
            state->finished = true;
            break;
        }

        slot_size = 0;
        for (i = 0; i < slot->tts_tupleDescriptor->natts; i++) {
            int16_t attlen = slot->tts_tupleDescriptor->attrs[i].attlen;

            if (attlen != -1)
                slot_size += attlen;
            else
                slot_size += 2 + VARSIZE_ANY(slot->tts_values[i]);
        }

        if (count + 2 + slot_size > aga.buf_size) {
            state->last_slot = slot;
            break;
        }

        *(uint16_t*)(state->buf + count) = slot_size;
        count += 2;

        for (i = 0; i < slot->tts_tupleDescriptor->natts; i++) {
            int16_t attlen = slot->tts_tupleDescriptor->attrs[i].attlen;

            if (attlen != -1) {
                if (slot->tts_tupleDescriptor->attrs[i].attbyval)
                    memcpy(state->buf + count, &slot->tts_values[i], attlen);
                else
                    memcpy(state->buf + count, (void*)slot->tts_values[i],
                           attlen);

                count += attlen;
            } else {
                attlen = VARSIZE_ANY(slot->tts_values[i]);

                *(uint16_t*)(state->buf + count) = attlen;
                count += 2;

                memcpy(state->buf + count, VARDATA(slot->tts_values[i]),
                       attlen);
                count += attlen;
            }
        }

        if (count >= aga.buf_size) break;
    }

    if (count > 0) {
        spu_write(FD_HOST_MEM, state->buf, (count > 64) ? count : 64, aga.buf);
    }

    return count;
}

void storpu_aggregate_end(unsigned long arg)
{
    struct aggregate_state* state = (struct aggregate_state*)arg;

    agg_end(state->agg);

    if (state->buf) munmap(state->buf, state->buf_size);
    free(state);
}

struct indexscan_state {
    IndexScanDesc scan;
    SnapshotData snapshot;
    ScanKey scankey;
    unsigned int nscankey;
    void* buf;
    size_t buf_size;
    bool finished;
    HeapTuple last_htup;
};

struct indexscan_state* storpu_index_beginscan(unsigned long arg)
{
    struct storpu_index_beginscan_arg ibsa;
    struct storpu_scankey* sskey;
    struct indexscan_state* state;
    int i;

    spu_read(FD_SCRATCHPAD, &ibsa, sizeof(ibsa), arg);

    state = malloc(sizeof(*state));
    if (!state) return NULL;

    memset(state, 0, sizeof(*state));

    state->nscankey = ibsa.num_scankeys;

    if (state->nscankey > 0) {
        state->scankey = malloc(sizeof(ScanKeyData) * state->nscankey);
        memset(state->scankey, 0, sizeof(ScanKeyData) * state->nscankey);
    } else {
        state->scankey = NULL;
    }

    state->scan = index_beginscan(ibsa.heap_relation, ibsa.index_relation,
                                  &state->snapshot, state->nscankey);

    return state;
}

void storpu_index_rescan(unsigned long arg)
{
    struct storpu_index_rescan_arg irsa;
    struct storpu_scankey* sskey;
    struct indexscan_state* state;
    int i;

    spu_read(FD_SCRATCHPAD, &irsa, sizeof(irsa), arg);

    state = (struct indexscan_state*)irsa.scan_state;

    for (i = 0; i < state->nscankey; i++) {
        if (state->scankey[i].sk_flags & SSK_REF_ARG)
            free((void*)state->scankey[i].sk_argument);
        state->scankey[i].sk_flags = 0;
    }

    if (state->nscankey > 0) {
        sskey = malloc(irsa.num_scankeys * sizeof(struct storpu_scankey));
        spu_read(FD_SCRATCHPAD, sskey,
                 irsa.num_scankeys * sizeof(struct storpu_scankey),
                 arg + sizeof(irsa));

        for (i = 0; i < state->nscankey; i++) {
            struct storpu_scankey* key = &sskey[i];
            const FmgrBuiltin* builtin = fmgr_isbuiltin(key->func);
            Datum arg = key->arg;

            if (key->flags & SSK_REF_ARG) {
                arg = (Datum)malloc(key->arglen);
                spu_read(FD_SCRATCHPAD, (void*)arg, key->arglen, key->arg);
            }

            ScanKeyInit(&state->scankey[i], key->attr_num, key->strategy,
                        builtin->func, arg);
            state->scankey[i].sk_flags = key->flags;
        }

        free(sskey);
    }

    index_rescan(state->scan, state->scankey, state->nscankey);

    state->finished = false;
    state->last_htup = NULL;
}

size_t storpu_index_getnext(unsigned long arg)
{
    struct storpu_index_getnext_arg iga;
    struct indexscan_state* state;

    spu_read(FD_SCRATCHPAD, &iga, sizeof(iga), arg);
    state = (struct indexscan_state*)iga.scan_state;

    if ((state->buf == NULL) || (state->buf_size != iga.buf_size)) {
        if (state->buf) munmap(state->buf, state->buf_size);

        state->buf = mmap(
            NULL, iga.buf_size, PROT_READ | PROT_WRITE,
            MAP_PRIVATE | MAP_ANONYMOUS | MAP_POPULATE | MAP_CONTIG, -1, 0);
        state->buf_size = iga.buf_size;
    }

    if (state->finished) return 0;

    size_t count = 0;

    while (iga.limit > 0) {
        HeapTuple htup;

        if (state->last_htup) {
            htup = state->last_htup;
            state->last_htup = NULL;
        } else {
            htup = index_getnext_slot(state->scan, ForwardScanDirection);
        }

        Datum values[8];
        bool isnull[8];
        heap_deform_tuple(htup, &tpcc_orders1_schema, values, isnull);
        printf("%ld %ld %ld %ld\n", values[0], values[1], values[2], values[3]);

        if (!htup) {
            state->finished = true;
            break;
        }

        if (count + 2 + htup->t_len > iga.buf_size) {
            state->last_htup = htup;
            break;
        }

        iga.limit--;

        *(uint16_t*)(state->buf + count) = htup->t_len;
        count += 2;
        memcpy(state->buf + count, htup->t_data, htup->t_len);
        count += htup->t_len;

        if (count >= iga.buf_size) break;
    }

    if (count > 0) {
        spu_write(FD_HOST_MEM, state->buf, count, iga.buf);
    }

    return count;
}

void storpu_index_endscan(unsigned long arg)
{
    struct indexscan_state* state = (struct indexscan_state*)arg;
    int i;

    index_endscan(state->scan);

    for (i = 0; i < state->nscankey; i++) {
        if (state->scankey[i].sk_flags & SSK_REF_ARG)
            free((void*)state->scankey[i].sk_argument);
    }
    free(state->scankey);

    if (state->buf) munmap(state->buf, state->buf_size);
    free(state);
}
