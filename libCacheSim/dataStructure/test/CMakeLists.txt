add_executable(testCHashtable test_concurrent_hashtable.c)
target_link_libraries(testCHashtable dataStructure utils cachelib pthread)

add_test(NAME testCHashtable COMMAND testCHashtable)
