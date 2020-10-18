[Streams::] Streams.

@Purpose: Support for writing structured textual output, perhaps to the screen,
to a file, or to a flexible-sized wide string.

@p About streams.
The Inform tools produce textual output in many formats (HTML, EPS, plain
text, Inform 6 code, XML, and so on), writing to a variety of files, and
often need to juggle and rearrange partially written segments. These
texts tend to be structured with running indentation, and may eventually
need to be written to a disc file with either ISO Latin-1 or UTF-8
encodings, perhaps escaped for XML.

"Streams" are an abstraction to make it easy to handle all of this. The
writer to a stream never needs to know where the text will come out, or how
it should be indented or encoded.

Moreover, streams unify text files and strings, and can hold arbitrary
Unicode text. This text is encoded internally as a sequence of 32-bit Unicode
code points, in a form we might call semi-composed, in that all possible
compositions of ISO Latin-1 characters are made: e.g., E plus acute accent
is composed to a single code point as E-acute.

We give just one character value a non-Unicode meaning:

@d NEWLINE_IN_STRING ((char) 0x7f) /* Within quoted text, all newlines are converted to this */

@ The |text_stream| type began as a generalisation of the standard C library's
|FILE|, and it is used in mostly similar ways. The user -- the whole
program outside of this section -- deals only with |text_stream *| pointers to
represent streams in use.

All stream handling is defined via macros. While many operations could be
handled by ordinary functions, others cannot. |text_stream| cannot have exactly
the semantics of |FILE| since we cannot rely on the host operating system
to allocate and deallocate the structures behind |text_stream *| pointers; and
we cannot use our own memory system, either, since we need stream handling
to work both before the memory allocator starts and after it has finished.
Our macros allow us to hide all this. Besides that, a macro approach makes it
easier to retrofit new implementations as necessary. (The present
implementation is the second stab at it.)

@ The main purpose of many functions is to write textual material to some
file. Such functions almost always have a special argument in their
prototypes: |OUTPUT_STREAM|. This tells them where to pipe their output, which
is always to a "current stream" called |OUT|. What this leads to, and who will
see that it's properly opened and closed, are not their concern.

@d OUTPUT_STREAM text_stream *OUT /* used only as a function prototype argument */

@ Three output streams are always open. One is |NULL|, that is, its value
as a |text_stream *| pointer is |NULL|, the generic C null pointer. This represents
an oubliette: it is entirely valid to use it, but output sent to |NULL| will
never be seen again.

The others are |STDOUT| and |STDERR|. As the names suggest these are wrappers
for |stdout| and |stderr|, the standard console output and error messages
"files" provided by the C library.

We should always use |PRINT(...)| instead of |printf(...)| for console output,
so that there are no uses of |printf| anywhere in the program.

@d STDOUT Streams::get_stdout()
@d STDERR Streams::get_stderr()

@ |PUT| and |PUT_TO| similarly print single characters, which are
specified as unsigned integer values. In practice, |WRITE_TO| and
|PUT_TO| are seldom needed because there is almost always only one
stream of interest at a time -- |OUT|, the current stream.

@d PUT(c) Streams::putc(c, OUT)

@d PUT_TO(stream, c) Streams::putc(c, stream)

@ Each stream has a current indentation level, initially 0. Lines of text
will be indented by one tab stop for each level; it's an error for the level
to become negative.

@d INDENT Streams::indent(OUT);
@d STREAM_INDENT(x) Streams::indent(x);
@d OUTDENT Streams::outdent(OUT);
@d STREAM_OUTDENT(x) Streams::outdent(x);
@d SET_INDENT(N) Streams::set_indentation(OUT, N);

@ Other streams only exist when explicitly created, or "opened". A function
is only allowed to open a new stream if it can be proved that this stream will
always subsequently be "closed". (Except for the possibility of the tool
halting with an internal error, and therefore an |exit(1)|, while the stream
is still open.) A stream can be opened and closed only once, and outside that
time its state is undefined: it must not be used at all.

The simplest way is to make a temporary stream, which can be used as a sort
of clipboard. For instance, suppose we have to compile X before Y, but have to
ensure Y comes before X in the eventual output. We create a temporary stream,
compile X into it, then compile Y to |OUT|, then copy the temporary stream
into |OUT| and dispose of it.

Temporary streams are always created in memory, held in C's local stack frame
rather than allocated and freed via |malloc| and |free|. It must always be
possible to prove that execution passes from |TEMPORARY_TEXT| to
|DISCARD_TEXT|, unless the program makes a fatal exit in between. The stream,
let's call it |TEMP|, exists only between those macros. We can legitimately
create a temporary stream many times in one function (for instance inside a
loop body) because each time |TEMP| is created as a new stream, overwriting the
old one. |TEMP| is a different stream each time it is created, so it does
not violate the rule that every stream is opened and closed once only.

@d TEMPORARY_TEXT(T)
	wchar_t T##_dest[2048];
	text_stream T##_stream_structure = Streams::new_buffer(2048, T##_dest);
	text_stream *T = &T##_stream_structure;

@d DISCARD_TEXT(T)
	STREAM_CLOSE(T);

@ Older code traditionally uses the following:

@d TEMPORARY_STREAM TEMPORARY_TEXT(TEMP)
@d CLOSE_TEMPORARY_STREAM DISCARD_TEXT(TEMP)

@ Otherwise we can create new globally existing streams, provided we take on
the responsibility for seeing that they are properly closed. There are two
choices: a stream in memory, allocated via |malloc| and freed by |free| when
the stream is closed; or a file written to disc, opened via |fopen| and
later closed by |fclose|. Files are always written in text mode, that is,
|"w"| not |"wb"|, for those platforms where this makes a difference.

We use streams to handle all of our text file output, so there should be no
calls to |fprintf| anywhere in the program except for binary files.

@d STREAM_OPEN_TO_FILE(new, fn, enc) Streams::open_to_file(new, fn, enc)

@d STREAM_OPEN_TO_FILE_APPEND(new, fn, enc) Streams::open_to_file_append(new, fn, enc)

@d STREAM_OPEN_IN_MEMORY(new) Streams::open_to_memory(new, 20480)

@d SMALL_STREAM_OPEN_IN_MEMORY(new) Streams::open_to_memory(new, 1024)

@d STREAM_CLOSE(stream) Streams::close(stream)

@ Finally, we can dynamically create streams using the memory manager:

@d STREAM_NEW CREATE(text_stream)

@ The following operation is equivalent to |fflush| and makes it more likely
(I put it no higher) that the text written to a stream has all actually been
copied onto the disc, rather than sitting in some operating system buffer.
This helps ensure that any debugging log is up to the minute, in case of
a crash, but its absence wouldn't hurt our normal function. Flushing
a memory stream is legal but does nothing.

@d STREAM_FLUSH(stream) Streams::flush(stream)

@ A piece of information we can read for any stream is the number of characters
written to it: its "extent". In fact, UTF-8 multi-byte encoding schemes,
together with differing platform interpretations of C's |'\n'|, mean that this
extent is not necessarily either the final file size in bytes or the final
number of human-readable characters. We will only actually use it to detect
whether text has, or has not, been written to a stream between two points in
time, by seeing whether or not it has increased.

@d STREAM_EXTENT(x) Streams::get_position(x)

@ The remaining operations are available only for streams in memory (well, and
for |NULL|, but of course they do nothing when applied to that). While they
could be provided for file streams, this would be so inefficient that we will
pretend it is impossible. Any function which might need to use one
of these operations should open with the following sentinel macro:

@d STREAM_MUST_BE_IN_MEMORY(x)
	if ((x != NULL) && (x->write_to_memory == NULL))
		internal_error("text_stream not in memory");

@ First, we can erase one or more recently written characters:

@d STREAM_BACKSPACE(x) Streams::set_position(x, Streams::get_position(x) - 1)

@d STREAM_ERASE_BACK_TO(start_position) Streams::set_position(OUT, start_position)

@ Second, we can look at the text written. The minimal form is to look at
just the most recent character, but we can also copy one entire memory
stream into another stream (where the target can be either a memory or file
stream).

@d STREAM_MOST_RECENT_CHAR(x) Streams::latest(x)

@d STREAM_COPY(to, from) Streams::copy(to, from)

@ So much for the definition; now the implementation. Here is the |text_stream|
structure. Open memory streams are represented by structures where
|write_to_memory| is valid, open file streams by those where |write_to_file|
is valid. That counts every open stream except |NULL|, which of course
doesn't point to a |text_stream| structure at all.

Any stream can have |USES_XML_ESCAPES_STRF| set or cleared. When this is set, the
XML (and HTML) escapes of |&amp;| for ampersand, and |&lt;| and |&gt;| for
angle brackets, will be used automatically on writing. By default this flag
is clear, that is, no conversion is made.

@d MALLOCED_STRF			0x00000001 /* was the |write_to_memory| pointer claimed by |malloc|? */
@d USES_XML_ESCAPES_STRF	0x00000002 /* see above */
@d USES_LOG_ESCAPES_STRF	0x00000004 /* |WRITE| to this stream supports |$| escapes */
@d INDENT_PENDING_STRF		0x00000008 /* we have just ended a line, so further text should indent */
@d FILE_ENCODING_ISO_STRF	0x00000010 /* relevant only for file streams */
@d FILE_ENCODING_UTF8_STRF	0x00000020 /* relevant only for file streams */

@d INDENTATION_BASE_STRF	0x00010000 /* number of tab stops in from the left margin */
@d INDENTATION_MASK_STRF	0x0FFF0000 /* (held in these bits) */

@c
typedef struct text_stream {
	int stream_flags; /* bitmap of the |*_STRF| values above */
	FILE *write_to_file; /* for an open stream, exactly one of these is |NULL| */
	struct HTML_file_state *as_HTML; /* relevant only to the |HTML::| section */
	wchar_t *write_to_memory;
	struct filename *file_written; /* ditto */
	int chars_written; /* number of characters sent, counting |\n| as 1 */
	int chars_capacity; /* maximum number the stream can accept without claiming more resources */
	struct text_stream *stream_continues; /* if one memory stream is extended by another */
} text_stream;

@ When text is stored at |write_to_memory|, it is kept as a zero-terminated C
wide string, with one word per Unicode code point. It turns out to be
efficient to preserve a small margin of clear space at the end of the space,
so out of the |chars_capacity| space, the following amount will be kept clear:

@d SPACE_AT_END_OF_STREAM 6

@ A statistic we keep, since it costs little:

@c
int total_file_writes = 0; /* number of text files opened for writing during the run */

@p Initialising the stream structure.
Note that the following fills in sensible defaults for every field, but the
result is not a valid open stream; it's a blank form ready to be filled in.

By default the upper limit on file size is 2 GB. It's very hard to see this
ever being approached for text files whose size is proportionate to the result
of human writing. The only output file with a sorceror's-apprentice-like
ability to grow and grow is the debugging file, and if it should reach 2 GB
then it {\it deserves} to be truncated and we will shed no tears.

@c
void Streams::initialise(text_stream *stream) {
	if (stream == NULL) internal_error("tried to initialise NULL stream");
	stream->stream_flags = 0;
	stream->write_to_file = NULL;
	stream->write_to_memory = NULL;
	stream->chars_written = 0;
	stream->chars_capacity = 2147483647;
	stream->stream_continues = NULL;
	stream->as_HTML = NULL;
	stream->file_written = NULL;
}

@ Once initialised, the stream can be tweaked:

@c
void Streams::enable_debugging(text_stream *stream) {
	stream->stream_flags |= USES_LOG_ESCAPES_STRF;
}

void Streams::declare_as_HTML(text_stream *stream, HTML_file_state *hs) {
	stream->as_HTML = hs;
}

HTML_file_state *Streams::get_HTML_file_state(text_stream *stream) {
	return stream->as_HTML;
}

@p Logging.

@c
void Streams::log(OUTPUT_STREAM, void *vS) {
	text_stream *stream = (text_stream *) vS;
	if (stream == NULL) {
		WRITE("NULL");
	} else if (stream->write_to_file) {
		WRITE("F'%f'(%d)", stream->file_written, stream->chars_written);
	} else {
		WRITE("S%x(", (long int) stream);
		while (stream) {
			WRITE("%d/%d", stream->chars_written, stream->chars_capacity);
			if (stream->stream_continues) WRITE("+");
			stream = stream->stream_continues;
		}
		WRITE(")");
	}
}

@p Standard I/O wrappers.
The first call to |Streams::get_stdout()| creates a suitable wrapper for |stdout|
and returns a |text_stream *| pointer to it; subsequent calls just return this wrapper.

@c
text_stream STDOUT_struct; int stdout_wrapper_initialised = FALSE;
text_stream *Streams::get_stdout(void) {
	if (stdout_wrapper_initialised == FALSE) {
		Streams::initialise(&STDOUT_struct); STDOUT_struct.write_to_file = stdout;
		stdout_wrapper_initialised = TRUE;
		#ifdef LOCALE_IS_ISO
		STDOUT_struct.stream_flags |= FILE_ENCODING_ISO_STRF;
		#endif
		#ifdef LOCALE_IS_UTF8
		STDOUT_struct.stream_flags |= FILE_ENCODING_UTF8_STRF;
		#endif
	}
	return &STDOUT_struct;
}

@ And similarly for the standard error file.

@c
text_stream STDERR_struct; int stderr_wrapper_initialised = FALSE;
text_stream *Streams::get_stderr(void) {
	if (stderr_wrapper_initialised == FALSE) {
		Streams::initialise(&STDERR_struct); STDERR_struct.write_to_file = stderr;
		stderr_wrapper_initialised = TRUE;
		#ifdef LOCALE_IS_ISO
		STDERR_struct.stream_flags |= FILE_ENCODING_ISO_STRF;
		#endif
		#ifdef LOCALE_IS_UTF8
		STDERR_struct.stream_flags |= FILE_ENCODING_UTF8_STRF;
		#endif
	}
	return &STDERR_struct;
}

@p Creating file streams.
Note that this can fail, if the host filing system refuses to open the file,
so we return |TRUE| if and only if successful.

@c
int Streams::open_to_file(text_stream *stream, filename *name, int encoding) {
	if (stream == NULL) internal_error("tried to open NULL stream");
	if (name == NULL) internal_error("stream_open_to_file on null filename");
	Streams::initialise(stream);
	switch(encoding) {
		case UTF8_ENC: stream->stream_flags |= FILE_ENCODING_UTF8_STRF; break;
		case ISO_ENC: stream->stream_flags |= FILE_ENCODING_ISO_STRF; break;
		default: internal_error("stream has unknown text encoding");
	}
	stream->write_to_file = Platform::iso_fopen(name, "w");
	if (stream->write_to_file == NULL) return FALSE;
	stream->file_written = name;
	total_file_writes++;
	return TRUE;
}

@ Similarly for appending:

@c
int Streams::open_to_file_append(text_stream *stream, filename *name, int encoding) {
	if (stream == NULL) internal_error("tried to open NULL stream");
	if (name == NULL) internal_error("stream_open_to_file on null filename");
	Streams::initialise(stream);
	switch(encoding) {
		case UTF8_ENC: stream->stream_flags |= FILE_ENCODING_UTF8_STRF; break;
		case ISO_ENC: stream->stream_flags |= FILE_ENCODING_ISO_STRF; break;
		default: internal_error("stream has unknown text encoding");
	}
	stream->write_to_file = Platform::iso_fopen(name, "a");
	if (stream->write_to_file == NULL) return FALSE;
	total_file_writes++;
	return TRUE;
}

@p Creating memory streams.
Here we have a choice. One option is to use |malloc| to allocate memory to hold
the text of the stream; this too can fail for host platform reasons, so again
we return a success code.

@c
int Streams::open_to_memory(text_stream *stream, int capacity) {
	if (stream == NULL) internal_error("tried to open NULL stream");
	if (capacity < SPACE_AT_END_OF_STREAM) capacity += SPACE_AT_END_OF_STREAM;
	Streams::initialise(stream);
	stream->write_to_memory = Memory::I7_calloc(capacity, sizeof(wchar_t), STREAM_MREASON);
	if (stream->write_to_memory == NULL) return FALSE;
	(stream->write_to_memory)[0] = 0;
	stream->stream_flags |= MALLOCED_STRF;
	stream->chars_capacity = capacity;
	return TRUE;
}

@ The other option avoids |malloc| by using specific storage already available.
If called validly, this cannot fail.

@c
text_stream Streams::new_buffer(int capacity, wchar_t *at) {
	if (at == NULL) internal_error("tried to make stream wrapper for NULL string");
	if (capacity < SPACE_AT_END_OF_STREAM)
		internal_error("memory stream too small");
	text_stream stream;
	Streams::initialise(&stream);
	stream.write_to_memory = at;
	(stream.write_to_memory)[0] = 0;
	stream.chars_capacity = capacity;
	return stream;
}

@p Converting from C strings.
We then have three ways to open a stream whose initial contents are given
by a C string. First, a wide string (a sequence of 32-bit Unicode code
points, null terminated):

@c
int Streams::open_from_wide_string(text_stream *stream, wchar_t *c_string) {
	if (stream == NULL) internal_error("tried to open NULL stream");
	int capacity = (c_string)?((int) wcslen(c_string)):0;
	@<Ensure a capacity large enough to hold the initial string in one frame@>;
	if (c_string) Streams::write_wide_string(stream, c_string);
	return TRUE;
}

void Streams::write_wide_string(text_stream *stream, wchar_t *c_string) {
	for (int i=0; c_string[i]; i++) Streams::putc(c_string[i], stream);
}

@ Similarly, an ISO string (a sequence of 8-bit code points in the first
page of the Unicode set, null terminated):

@c
int Streams::open_from_ISO_string(text_stream *stream, char *c_string) {
	if (stream == NULL) internal_error("tried to open NULL stream");
	int capacity = (c_string)?((int) CStrings::strlen_unbounded(c_string)):0;
	@<Ensure a capacity large enough to hold the initial string in one frame@>;
	if (c_string) Streams::write_ISO_string(stream, c_string);
	return TRUE;
}

void Streams::write_ISO_string(text_stream *stream, char *c_string) {
	for (int i=0; c_string[i]; i++) Streams::putc(c_string[i], stream);
}

@ Finally, a UTF-8 encoded C string:

@c
int Streams::open_from_UTF8_string(text_stream *stream, char *c_string) {
	if (stream == NULL) internal_error("tried to open NULL stream");
	int capacity = (c_string)?((int) CStrings::strlen_unbounded(c_string)):0;
	@<Ensure a capacity large enough to hold the initial string in one frame@>;
	if (c_string) Streams::write_UTF8_string(stream, c_string);
	return TRUE;
}

void Streams::write_UTF8_string(text_stream *stream, char *c_string) {
	unicode_file_buffer ufb = TextFiles::create_ufb();
	int c;
	while ((c = TextFiles::utf8_fgetc(NULL, &c_string, FALSE, &ufb)) != 0)
		Streams::putc(c, stream);
}

@ ...all of which use:

@<Ensure a capacity large enough to hold the initial string in one frame@> =
	if (capacity < 8) capacity = 8;
	capacity += 1+SPACE_AT_END_OF_STREAM;
	int rv = Streams::open_to_memory(stream, capacity);
	if (rv == FALSE) return FALSE;

@p Converting to C strings.
Now for the converse problem. 

@c
void Streams::write_as_wide_string(wchar_t *C_string, text_stream *stream, int buffer_size) {
	if (buffer_size == 0) return;
	if (stream == NULL) { C_string[0] = 0; return; }
	if (stream->write_to_file) internal_error("stream_get_text on file stream");
	int i = 0;
	while (stream) {
		for (int j=0; j<stream->chars_written; j++) {
			if (i >= buffer_size-1) break;
			C_string[i++] = stream->write_to_memory[j];
		}
		stream = stream->stream_continues;
	}
	C_string[i] = 0;
}

@ Unicode code points outside the first page are flattened to |'?'| in an
ISO string:

@c
void Streams::write_as_ISO_string(char *C_string, text_stream *stream, int buffer_size) {
	if (buffer_size == 0) return;
	if (stream == NULL) { C_string[0] = 0; return; }
	if (stream->write_to_file) internal_error("stream_get_text on file stream");
	int i = 0;
	while (stream) {
		for (int j=0; j<stream->chars_written; j++) {
			if (i >= buffer_size-1) break;
			wchar_t c = stream->write_to_memory[j];
			if (c < 256) C_string[i++] = (char) c; else C_string[i++] = '?';
		}
		stream = stream->stream_continues;
	}
	C_string[i] = 0;
}

@ Unicode code points outside the first page are flattened to |'?'| in an
ISO string:

@c
void Streams::write_as_UTF8_string(char *C_string, text_stream *stream, int buffer_size) {
	if (buffer_size == 0) return;
	if (stream == NULL) { C_string[0] = 0; return; }
	if (stream->write_to_file) internal_error("stream_get_text on file stream");
	unsigned char *to = (unsigned char *) C_string;
	int i = 0;
	while (stream) {
		for (int j=0; j<stream->chars_written; j++) {
			unsigned int c = (unsigned int) stream->write_to_memory[j];
			if (c >= 0x800) {
				if (i >= buffer_size-3) break;
				to[i++] = 0xE0 + (unsigned char) (c >> 12);
				to[i++] = 0x80 + (unsigned char) ((c >> 6) & 0x3f);
				to[i++] = 0x80 + (unsigned char) (c & 0x3f);
			} else if (c >= 0x80) {
				if (i >= buffer_size-2) break;
				to[i++] = 0xC0 + (unsigned char) (c >> 6);
				to[i++] = 0x80 + (unsigned char) (c & 0x3f);
			} else {
				if (i >= buffer_size-1) break;
				to[i++] = (unsigned char) c;
			}
		}
		stream = stream->stream_continues;
	}
	to[i] = 0;
}

@p Locale versions.

@c
int Streams::open_from_locale_string(text_stream *stream, char *C_string) {
	#ifdef LOCALE_IS_UTF8
	return Streams::open_from_ISO_string(stream, C_string);
	#endif
	#ifdef LOCALE_IS_ISO
	return Streams::open_from_ISO_string(stream, C_string);
	#endif
}

void Streams::write_as_locale_string(char *C_string, text_stream *stream, int buffer_size) {
	#ifdef LOCALE_IS_UTF8
	Streams::write_as_UTF8_string(C_string, stream, buffer_size);
	#endif
	#ifdef LOCALE_IS_ISO
	Streams::write_as_ISO_string(C_string, stream, buffer_size);
	#endif
}

void Streams::write_locale_string(text_stream *stream, char *C_string) {
	#ifdef LOCALE_IS_UTF8
	Streams::write_UTF8_string(stream, C_string);
	#endif
	#ifdef LOCALE_IS_ISO
	Streams::write_ISO_string(stream, C_string);
	#endif
}

@p Flush and close.
Note that flush is an operation which can be performed on any stream, including
|NULL|:

@c
void Streams::flush(text_stream *stream) {
	if (stream == NULL) return;
	if (stream->write_to_file) fflush(stream->write_to_file);
}

@ But closing is not allowed for |NULL| or the standard I/O wrappers:

@c
void Streams::close(text_stream *stream) {
	if (stream == NULL) internal_error("tried to close NULL stream");
	if (stream == &STDOUT_struct) internal_error("tried to close STDOUT stream");
	if (stream == &STDERR_struct) internal_error("tried to close STDERR stream");
	if (stream->chars_capacity == -1) internal_error("stream closed twice");
	if (stream->stream_continues) {
		Streams::close(stream->stream_continues);
		stream->stream_continues = NULL;
	}
	stream->chars_capacity = -1; /* mark as closed */
	if (stream->write_to_file) @<Take suitable action to close the file stream@>;
	if (stream->write_to_memory) @<Take suitable action to close the memory stream@>;
}

@ Note that we need do nothing to close a memory stream when the storage
was supplied by our client; it only needs freeing if we were the ones who
allocated it.

Inscrutably, |fclose| returns |EOF| to report any failure.

@<Take suitable action to close the file stream@> =
	if ((ferror(stream->write_to_file)) || (fclose(stream->write_to_file) == EOF))
		Errors::fatal("The host computer reported an error trying to write a text file");
	if (stream != DL)
		LOGIF(TEXT_FILES, "Text file '%f' (%s): %d characters written\n",
			stream->file_written,
			(stream->stream_flags & FILE_ENCODING_UTF8_STRF)?"UTF8":"ISO",
			stream->chars_written);
	stream->write_to_file = NULL;

@ Note that we need do nothing to close a memory stream when the storage
was supplied by our client; it only needs freeing if we were the ones who
allocated it. |free| is a void function; in theory it cannot fail, if
supplied a valid argument.

We have to be very careful once we have called |free|, because that memory
may well contain the |text_stream| structure to which |stream| points -- see
how continuations are made, below.

@<Take suitable action to close the memory stream@> =
	if ((stream->stream_flags) & MALLOCED_STRF) {
		Memory::I7_free(stream->write_to_memory, STREAM_MREASON);
		stream->write_to_memory = NULL;
		stream = NULL;
	}

@p Writing.
The following function is modelled on the "minimum |printf|" function
used as an example in Kernighan and Ritchie, Chapter 7. All text in the
formatting string is treated as raw except for |%| escapes.

@c
typedef void (*writer_function)(text_stream *, char *, void *);
typedef void (*log_function)(text_stream *, void *);
int escapes_registered = FALSE;
writer_function the_escapes[256];
int log_escapes_registered = FALSE;
log_function log_escapes[256];

void Streams::register_writer(int esc, void (*f)(text_stream *, char *, void *)) {
	if (escapes_registered == FALSE) {
		escapes_registered = TRUE;
		for (int i=0; i<256; i++) the_escapes[i] = NULL;
		char *directly_handled = "cdgisx";
		for (int i=0; directly_handled[i]; i++) the_escapes[i] = &Streams::dummy_writer;
	}
	if ((esc < 0) || (esc > 255) || (Characters::isalpha((char) esc) == FALSE))
		internal_error("nonalphabetic escape");
	if ((the_escapes[esc]) && (the_escapes[esc] != f))
		internal_error("clash of escapes");
	the_escapes[esc] = f;
}

void Streams::dummy_writer(OUTPUT_STREAM, char *format_string, void *data) {
	internal_error("error in stream writer");
}

@ This doesn't seem to belong anywhere else:

@c
void Streams::write_wide_C_string(OUTPUT_STREAM, char *format_string, void *vW) {
	wchar_t *W = (wchar_t *) vW;
	for (int j = 0; W[j]; j++) PUT(W[j]);
}

@

@c
void Streams::register_log_writer(int esc, void (*f)(text_stream *, void *)) {
	if (log_escapes_registered == FALSE) {
		log_escapes_registered = TRUE;
		for (int i=0; i<256; i++) log_escapes[i] = NULL;
	}
	if ((esc < 0) || (esc > 255) || (Characters::isalpha((char) esc) == FALSE))
		internal_error("nonalphabetic escape");
	if ((log_escapes[esc]) && (log_escapes[esc] != f))
		internal_error("clash of escapes");
	log_escapes[esc] = f;
}

void Streams::printf(text_stream *stream, char *fmt, ...) {
	va_list ap; /* the variable argument list signified by the dots */
	char *p;
	if (stream == NULL) return;
	va_start(ap, fmt); /* macro to begin variable argument processing */
	for (p = fmt; *p; p++) {
		switch (*p) {
			case '%': @<Recognise traditional printf escape sequences@>; break;
			case '$': @<Recognise debugging log escape sequences@>; break;
			default: Streams::putc(*p, stream); break;
		}
	}
	va_end(ap); /* macro to end variable argument processing */
}

@ We don't trouble to check that correct |printf| escapes have been used:
instead, we pass anything in the form of a percentage sign, followed by
up to four nonalphabetic modifying characters, followed by an alphabetic
category character for numerical printing, straight through to |sprintf|
or |fprintf|.

Thus an escape like |%04d| is handled by the standard C library, but not
|%s|, which we handle directly. That's for two reasons: first, we want to
be careful to prevent overruns of memory streams; second, we need to ensure
that the correct encoding is used when writing to disc. The numerical
escapes involve only characters whose representation is the same in all our
file encodings, but expanding |%s| does not.

@<Recognise traditional printf escape sequences@> =
	int ival; double dval; char *sval;
	char format_string[8], category = ' ';
	int i = 1;
	format_string[0] = '%';
	p++;
	while (*p) {
		format_string[i++] = *p;
		if ((islower(*p)) || (isupper(*p)) || (*p == '%')) category = *p;
		p++;
		if ((category != ' ') || (i==6)) break;
	}
	format_string[i] = 0; p--;
	#pragma clang diagnostic push
	#pragma clang diagnostic ignored "-Wformat-nonliteral"
	switch (category) {
		case 'c': case 'd': case 'i': case 'x': { /* |char| is promoted to |int| in variable arguments */
			ival = va_arg(ap, int);
			char temp[256];
			if (snprintf(temp, 255, format_string, ival) >= 255) strcpy(temp, "?");
			for (int j = 0; temp[j]; j++) Streams::putc(temp[j], stream);
			break;
		}
		case 'g': {
			dval = va_arg(ap, double);
			char temp[256];
			if (snprintf(temp, 255, format_string, dval) >= 255) strcpy(temp, "?");
			for (int j = 0; temp[j]; j++) Streams::putc(temp[j], stream);
			break;
		}
		case 's':
			for (sval = va_arg(ap, char *); *sval; sval++) Streams::putc(*sval, stream);
			break;
		case '%': Streams::putc('%', stream); break;
		case '$': Streams::putc('$', stream); break;
		default: {
			void *q = va_arg(ap, void *);
			int i = (int) category;
			if ((i<0) || (i > 255)) i = 0;
			writer_function f = the_escapes[i];
			if (f) {
				(*f)(stream, format_string, q);
				break;
			}
			fprintf(stderr, "*** Bad escape: <%s> ***\n", format_string);
			internal_error("Unknown % string escape in stream_printf");
		}
	}
	#pragma clang diagnostic pop

@

@<Recognise debugging log escape sequences@> =
	if ((stream->stream_flags) & USES_LOG_ESCAPES_STRF) {
		char category = *(++p);
		void *q = va_arg(ap, void *);
		int i = (int) category;
		if ((i<0) || (i > 255)) i = 0;
		log_function f = log_escapes[i];
		if (f) {
			(*f)(stream, q);
			break;
		}
		fprintf(stderr, "*** Bad log escape: <%c> ***\n", category);
		internal_error("Unknown $ string escape in log");		
	} else Streams::putc('$', stream);

@ Our equivalent of |fputc| reads:

@c
void Streams::putc(int c_int, text_stream *stream) {
	unsigned int c;
	if (c_int >= 0) c = (unsigned int) c_int; else c = (unsigned int) (c_int + 256);
	if (stream == NULL) return;
	text_stream *first_stream = stream;
	if (c != '\n') @<Insert indentation if this is pending@>;
	if ((stream->stream_flags) & USES_XML_ESCAPES_STRF) {
		switch(c) {
			case NEWLINE_IN_STRING: Streams::literal(stream, "<br>"); return;
			case '&': Streams::literal(stream, "&amp;"); return;
			case '<': Streams::literal(stream, "&lt;"); return;
			case '>': Streams::literal(stream, "&gt;"); return;
		}
	}
	while (stream->stream_continues) stream = stream->stream_continues;
	@<Ensure there is room to expand the escape sequence into@>;
	if (stream->write_to_file) {
		if (stream->stream_flags & FILE_ENCODING_UTF8_STRF)
			@<Put a UTF8-encoded character into the underlying file@>
		else if (stream->stream_flags & FILE_ENCODING_ISO_STRF) {
		 	if (c >= 0x100) c = '?';
			fputc((int) c, stream->write_to_file);
		} else internal_error("stream has unknown text encoding");
	} else if (stream->write_to_memory) {
		if ((c >= 0x0300) && (c <= 0x036F) && (stream->chars_written > 0)) {
			c = (unsigned int) Characters::combine_accent(
				(int) c, (stream->write_to_memory)[stream->chars_written - 1]);
			stream->chars_written--;
		}
		(stream->write_to_memory)[stream->chars_written] = (wchar_t) c;
	}
	if (c == '\n') first_stream->stream_flags |= INDENT_PENDING_STRF;
	stream->chars_written++;
}

@ Where we pack large character values, up to 65535, as follows.

@<Put a UTF8-encoded character into the underlying file@> =
	if (c >= 0x800) {
		fputc(0xE0 + (c >> 12), stream->write_to_file);
		fputc(0x80 + ((c >> 6) & 0x3f), stream->write_to_file);
		fputc(0x80 + (c & 0x3f), stream->write_to_file);
	} else if (c >= 0x80) {
		fputc(0xC0 + (c >> 6), stream->write_to_file);
		fputc(0x80 + (c & 0x3f), stream->write_to_file);
	} else fputc((int) c, stream->write_to_file);

@

@<Insert indentation if this is pending@> =
	if (first_stream->stream_flags & INDENT_PENDING_STRF) {
		first_stream->stream_flags -= INDENT_PENDING_STRF;
		int L = (first_stream->stream_flags & INDENTATION_MASK_STRF)/INDENTATION_BASE_STRF;
		for (int i=0; i<L; i++) {
			Streams::putc(' ', first_stream); Streams::putc(' ', first_stream);
			Streams::putc(' ', first_stream); Streams::putc(' ', first_stream);
		}
	}

@ The following is checked before any numerical |printf|-style escape is expanded
into the stream, or before any single character is written. Thus we cannot
overrun our buffers unless the expansion of a numerical escape exceeds
|SPACE_AT_END_OF_STREAM| plus 1 in size. Since no outside influence gets to
choose what formatting escapes we use (so that |%3000d|, say, can't occur),
we can be pretty confident.

The interesting case occurs when we run out of memory in a memory stream.
We make a continuation to a fresh |text_stream| structure, which points to twice
as much memory as the original, allocated via |malloc|. (We will actually need
a little more memory than that because we also have to make room for the
|text_stream| structure itself.) We then set |stream| to the |continuation|. Given
that |malloc| was successful -- and it must have been or we would have stopped
with a fatal error -- the continuation is guaranteed to be large enough,
since it's twice the size of the original, which itself was large enough to
hold any escape sequence when opened.

@<Ensure there is room to expand the escape sequence into@> =
	if (stream->chars_written + SPACE_AT_END_OF_STREAM >= stream->chars_capacity) {
		if (stream->write_to_file) return; /* write nothing further */
		if (stream->write_to_memory) {
			int offset = (32 + 2*(stream->chars_capacity))*((int) sizeof(wchar_t));
			int needed = offset + ((int) sizeof(text_stream)) + 32;
			void *further_allocation = Memory::I7_malloc(needed, STREAM_MREASON);
			if (further_allocation == NULL) Errors::fatal("Out of memory");
			text_stream *continuation = (text_stream *) (further_allocation + offset);
			Streams::initialise(continuation);
			continuation->write_to_memory = further_allocation;
			continuation->chars_capacity = 2*stream->chars_capacity;
			(continuation->write_to_memory)[0] = 0;
			stream->stream_continues = continuation;
			stream = continuation;
		}
	}

@ Literal printing is just printing with XML escapes switched off:

@c
void Streams::literal(text_stream *stream, char *p) {
	if (stream == NULL) return;
	int i, x = ((stream->stream_flags) & USES_XML_ESCAPES_STRF);
	if (x) stream->stream_flags -= USES_XML_ESCAPES_STRF;
	for (i=0; p[i]; i++) Streams::putc((int) p[i], stream);
	if (x) stream->stream_flags += USES_XML_ESCAPES_STRF;
}

@ Shifting indentation. For every indent there must be an equal and opposite
outdent, but error conditions can cause some compilation routines to issue
problem messages and then leave what they're doing incomplete, so we will
be a little cautious about assuming that a mismatch means an error.

@c
void Streams::indent(text_stream *stream) {
	if (stream == NULL) return;
	stream->stream_flags += INDENTATION_BASE_STRF;
}

void Streams::outdent(text_stream *stream) {
	if (stream == NULL) return;
	if ((stream->stream_flags & INDENTATION_MASK_STRF) == 0) {
		if (Errors::have_occurred() == FALSE) internal_error("stream indentation negative");
		return;
	}
	stream->stream_flags -= INDENTATION_BASE_STRF;
}

void Streams::set_indentation(text_stream *stream, int N) {
	if (stream == NULL) return;
	int B = stream->stream_flags & INDENTATION_MASK_STRF;
	stream->stream_flags -= B;
	stream->stream_flags += N*INDENTATION_BASE_STRF;
}

@ We can read the position for any stream, including |NULL|, but no matter
how much is written to |NULL| this position never budges.

Because of continuations, this is not as simple as returning the |chars_written|
field.

@c
int Streams::get_position(text_stream *stream) {
	int t = 0;
	while (stream) {
		t += stream->chars_written;
		stream = stream->stream_continues;
	}
	return t;
}

@p Memory-stream-only functions.
While it would be easy enough to implement this for file streams too, there's
no point, since it is used only in concert with backspacing.

@c
int Streams::latest(text_stream *stream) {
	if (stream == NULL) return 0;
	if (stream->write_to_file) internal_error("stream_latest on file stream");
	while ((stream->stream_continues) && (stream->stream_continues->chars_written > 0))
		stream = stream->stream_continues;
	if (stream->write_to_memory) {
		if (stream->chars_written > 0)
			return (int) ((stream->write_to_memory)[stream->chars_written - 1]);
	}
	return 0;
}

@ Accessing characters by index. Note that the stream terminates at the first
zero byte found, so that putting a zero truncates it.

@c
wchar_t Streams::get_char_at_index(text_stream *stream, int position) {
	if (stream == NULL) internal_error("examining null stream");
	if (stream->write_to_file) internal_error("examining file stream");
	while (position >= stream->chars_written) {
		position = position - stream->chars_written;
		stream = stream->stream_continues;
		if (stream == NULL) return 0;
	}
	return (stream->write_to_memory)[position];
}

void Streams::put_char_at_index(text_stream *stream, int position, wchar_t C) {
	if (stream == NULL) internal_error("modifying null stream");
	if (stream->write_to_file) internal_error("modifying file stream");
	while (position >= stream->chars_written) {
		position = position - stream->chars_written;
		stream = stream->stream_continues;
		if (stream == NULL) internal_error("overrun memory stream");
	}
	(stream->write_to_memory)[position] = C;
	if (C == 0) {
		stream->chars_written = position;
		if (stream->stream_continues) {
			Streams::close(stream->stream_continues);
			stream->stream_continues = NULL;
		}		
	}
}

@ Now for what is the trickiest function, because the position may be moved back
so that later continuations fall away. This will very rarely happen, so we
won't worry about the inefficiency of freeing up the memory saved by closing
such continuation blocks (which would be inefficient if we immediately had
to open similar ones again).

@c
void Streams::set_position(text_stream *stream, int position) {
	if (stream == NULL) return;
	if (position < 0) position = 0; /* to simplify the implementation of backspacing */
	if (stream->write_to_file) internal_error("stream_set_position on file stream");
	if (stream->write_to_memory) {
		while (position > stream->chars_written) {
			position = position - stream->chars_written;
			stream = stream->stream_continues;
			if (stream == NULL) internal_error("can't set position forwards");
		}
		stream->chars_written = position;
		(stream->write_to_memory)[stream->chars_written] = 0;
		if (stream->stream_continues) {
			Streams::close(stream->stream_continues);
			stream->stream_continues = NULL;
		}
	}
}

@ Lastly, our copying function, where |from| has to be a memory stream (or
|NULL|) but |to| can be anything (including |NULL|).

@c
void Streams::copy(text_stream *to, text_stream *from) {
	if ((from == NULL) || (to == NULL)) return;
	if (from->write_to_file) internal_error("stream_copy from file stream");
	if (from->write_to_memory) {
		int i;
		for (i=0; i<from->chars_written; i++) {
			int c = (int) ((from->write_to_memory)[i]);
			Streams::putc(c, to);
		}
		if (from->stream_continues) Streams::copy(to, from->stream_continues);
	}
}

void Streams::write(OUTPUT_STREAM, char *format_string, void *vS) {
	text_stream *S = (text_stream *) vS;
	Streams::copy(OUT, S);
}
