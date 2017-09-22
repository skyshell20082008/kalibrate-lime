bin_PROGRAMS = kal

kal_SOURCES = \
   arfcn_freq.cc \
   c0_detect.cc	 \
   circular_buffer.cc \
   fcch_detector.cc \
   kal.cc \
   offset.cc \
   lime_source.cc \
   convenience.cc\
   util.cc\
   arfcn_freq.h \
   c0_detect.h \
   convenience.h\
   circular_buffer.h \
   fcch_detector.h \
   offset.h \
   lime_complex.h \
   lime_source.h \
   util.h\
   version.h

kal_CXXFLAGS = $(FFTW3_CFLAGS) $(LIBLIMERF_CFLAGS)

if DARWIN
kal_LDADD = $(FFTW3_LIBS) $(LIBLIMERF_LIBS)
else
kal_LDADD = $(FFTW3_LIBS) $(LIBLIMERF_LIBS) -lrt
endif
