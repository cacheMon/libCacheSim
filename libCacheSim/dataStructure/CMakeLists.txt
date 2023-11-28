add_subdirectory(hash)
add_subdirectory(test)

set(source
        pqueue.c
        splay.c
        bloom.c
        minimalIncrementCBF.c
        hash/murmur3.c
        hashtable/chainedHashtable.c
        hashtable/chainedHashTableV2.c
        hashtable/cChainedHashTable.c
        )
add_library (dataStructure ${source})

