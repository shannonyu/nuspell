lib_LIBRARIES = libhunspell.a
libhunspell_includedir = $(includedir)/hunspell
libhunspell_a_SOURCES=affentry.cxx affentry.hxx affixmgr.cxx affixmgr.hxx \
                           atypes.hxx baseaffix.hxx csutil.cxx csutil.hxx \
                           filemgr.cxx filemgr.hxx hashmgr.cxx hashmgr.hxx \
                           htypes.hxx hunspell.cxx hunspell.h hunspell.hxx \
                           hunzip.cxx hunzip.hxx langnum.hxx \
                           phonet.cxx phonet.hxx replist.cxx replist.hxx \
                           suggestmgr.cxx suggestmgr.hxx utf_info.hxx w_char.hxx
nodist_libhunspell_a_SOURCES=hunvisapi.h


libhunspell_include_HEADERS=hunspell.hxx hunspell.h hunvisapi.h \
		                w_char.hxx atypes.hxx

# libhunspell_a_LDFLAGS=-no-undefined -version-info 0:1:0

AM_CPPFLAGS = $(CODE_COVERAGE_CPPFLAGS)
AM_CXXFLAGS = -std=c++14 $(CFLAG_VISIBILITY) -DBUILDING_LIBHUNSPELL \
              $(CODE_COVERAGE_CXXFLAGS)
LIBADD      = $(CODE_COVERAGE_LIBS)
