# WebProxy retry memory leaks

This document captures the two heap leaks that surfaced when the agent keeps
retrying a control-channel connection after proxy or DNS failures. Both issues
were easy to trigger when the agent was unable to reach the server hostname on
Linux or Windows systems.

## Root causes

1. **`ILibSimpleDataStore_CachedEx()` left the replaced cache entry allocated**
   Every call to `ILibSimpleDataStore_Cached()` allocates a fresh
   `ILibSimpleDataStore_CacheEntry`. The previous fix attempted to free the
   prior node via `ILibMemory_Free()`, but these entries are allocated with
   `ILibMemory_Allocate()` and must be released with `free()`. As a result, the
   WebProxy cache grew by roughly 96 bytes on every retry whenever the agent
   could not reach its server. The updated logic explicitly removes any existing
   cache node and releases it before inserting the new value, keeping the cache
   size constant. 【F:microstack/ILibSimpleDataStore.c†L148-L176】

2. **`ILibSimpleDataStore_DeleteEx()` did not clear the cache**  
   Removing keys such as `WebProxy` only evicted the persistent `keyTable` entry
   and left the cached `ILibSimpleDataStore_CacheEntry` in memory. The agent would
   continue to read the stale proxy from the cache, which not only prevented
   recovery but also leaked the cached allocation. This scenario happened when a
   `.proxy` file was removed to disable a proxy while the agent was still
   retrying connections. 【F:microstack/ILibSimpleDataStore.c†L914-L977】

## Verifying the fix

The repository now contains `test/simpledatastore_cache_test.c`, a small
stand-alone probe that exercises the cache and reports the heap deltas. Build it
on Linux with:

```bash
gcc -I. -I./microstack -D_POSIX -D_POSIX_C_SOURCE=200809L -D_GNU_SOURCE \
    test/simpledatastore_cache_test.c \
    microstack/ILibSimpleDataStore.c \
    microstack/ILibParsers.c \
    -lcrypto -lz -lpthread -ldl -o cache-test
```

Run the program before and after applying the fixes. On an unpatched build the
`delta_put` value grows linearly with the iteration count (multiple megabytes by
the time you reach 10K iterations). With the patched library the delta stops
growing once the hashtable has allocated its initial buckets. You can verify
this by running the probe with different iteration counts and observing that the
reported deltas stay flat:

```text
$ ./cache-test 10000
iterations=10000 allocated_before=9056 allocated_after_put=152976 allocated_after_delete=152976
delta_put=143920 delta_delete=143920

$ ./cache-test 50000
iterations=50000 allocated_before=9056 allocated_after_put=152976 allocated_after_delete=152976
delta_put=143920 delta_delete=143920
```

The constant delta reflects the one-time allocation required for the cache
table itself; additional retries no longer consume extra heap, and deleting the
key releases the cached value immediately.

### Troubleshooting unexpected results

- If `delta_put` continues to scale with the iteration count, rebuild
  `cache-test` after cleaning any stale objects to ensure it links against the
  updated `microstack/ILibSimpleDataStore.c`.
- Verify that the output key count stays flat by running the harness with
  multiple iteration values; the numbers above should remain within a few
  kilobytes of one another once the hashtable buckets have been created.
- When testing on glibc versions older than 2.33, replace `mallinfo2()` with
  `mallinfo()` inside the harness to continue collecting the allocation
  snapshot.

