# WebProxy retry memory leaks

This document captures the two heap leaks that surfaced when the agent keeps
retrying a control-channel connection after proxy or DNS failures. Both issues
were easy to trigger when the agent was unable to reach the server hostname on
Linux or Windows systems.

## Root causes

1. **`ILibSimpleDataStore_CachedEx()` dropped the previous cache node**  
   Every call to `ILibSimpleDataStore_Cached()` allocates a new
   `ILibSimpleDataStore_CacheEntry`. When the key already existed the hashtable
   returned the previous entry, but the caller ignored the return value. The old
   allocation therefore leaked on every retry. Repeated proxy auto-detection (or
   a manually configured proxy that keeps failing) steadily increases the
   process heap. 【F:microstack/ILibSimpleDataStore.c†L126-L177】

2. **`ILibSimpleDataStore_DeleteEx()` did not clear the cache**  
   Removing keys such as `WebProxy` only evicted the persistent `keyTable` entry
   and left the cached `ILibSimpleDataStore_CacheEntry` in memory. The agent would
   continue to read the stale proxy from the cache, which not only prevented
   recovery but also leaked the cached allocation. This scenario happened when a
   `.proxy` file was removed to disable a proxy while the agent was still
   retrying connections. 【F:microstack/ILibSimpleDataStore.c†L844-L905】

## Verifying the fix

The repository now contains `test/simpledatastore_cache_test.c`, a small
stand-alone probe that exercises the cache and reports the heap deltas. Build it
on Linux with:

```bash
gcc -I. -I./microstack -D_POSIX -D_POSIX_C_SOURCE=200809L -D_GNU_SOURCE \
    test/simpledatastore_cache_test.c \
    microstack/ILibSimpleDataStore.c \
    microstack/ILibParsers.c \
    microstack/ILibCrypto.c \
    -lssl -lcrypto -lz -lpthread -ldl -o cache-test
```

Run the program before and after applying the fixes. On an unpatched build the
`delta_put` value grows linearly with the iteration count (tens of megabytes
after 10K iterations). With the patched library both `delta_put` and
`delta_delete` hover near zero, demonstrating that the cache entries are now
reused and that deletions release the cached block:

```text
iterations=10000 allocated_before=249856 allocated_after_put=250016 allocated_after_delete=249872
delta_put=160 delta_delete=16
```

Use a larger iteration count (for example `./cache-test 50000`) if you want to
observe the unchecked growth on a baseline build. The same test also confirms
that deleting `WebProxy` now releases its cached entry.

