include_directories(pgtest/storpupg)

set(PGTEST_BACKEND "storpu" CACHE STRING "" FORCE)
add_subdirectory(pgtest)

add_subdirectory(postgres)


add_library(libtest SHARED test.c synthetic.c pgtest.c palloc.c fmgrtab.c misc.c)
set_target_properties(libtest PROPERTIES PREFIX "")
target_link_libraries(libtest -nostdlib storpupg pgtest storpu)
