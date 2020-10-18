[Log::] Debugging Log.

@Purpose: To write to the debugging log, a plain text file which traces what
we're doing, in order to assist those lost souls debugging it.

@p The DL stream.
The debugging log file occupies the following stream:

@c
text_stream debug_log_file_struct; /* The actual debugging log file */
text_stream *debug_log_file = &debug_log_file_struct; /* The actual debugging log file */
text_stream *DL = NULL; /* Current destination of debugging text: kept |NULL| until opened */

@p Macros.
"The most effective debugging tool is still careful thought, coupled with
judiciously placed print statements" (Brian Kernighan). I can usually only
manage one of these, but here goes.

To write to the debugging log, we must in principle write to a stream called
|DL|. In practice we more often use a pair of pseudo-functions called |LOG|
and |LOGIF|, which are macros defined below. For instance, the
pseudo-function-call

|LOGIF(WHATEVER, "Heading %d skipped\n", n);|

prints the line in question to the debugging log only if the aspect |WHATEVER|
is currently switched on. Plain |LOG| does the same, but unconditionally.

@d LOG_INDENT STREAM_INDENT(DL)
@d LOG_OUTDENT STREAM_OUTDENT(DL)

@p Debugging aspects.
There are many different things which can go into the debugging file, or
need not: for Inform even a simple half-page source can result in a
debugging log 5MB in size, so we generally don't want everything included
unless we ask for it.

A "debugging aspect" is a category of information that can be included, or
not, as we please. Each has a unique number and a name of up to three words in
length.

@c
typedef struct debugging_aspect {
	struct text_stream *hyphenated_name; /* e.g., "memory-usage" */
	struct text_stream *negated_name; /* e.g., "no-memory-usage" */
	struct text_stream *unhyphenated_name; /* e.g., "memory-usage" */
	int on_or_off; /* whether or not active when writing to debugging log */
	int alternate; /* whether or not active when writing in trace mode */
} debugging_aspect;

@ And now we must define all those constants and names. Note that the
|TRUE| or |FALSE| settings below are the defaults, and apply unless the
source says otherwise. The |alternate| settings are those used in
trace-sentences mode, that is, between asterisk sentences.

@d DEBUGGING_LOG_INCLUSIONS_DA 0
@d SHELL_USAGE_DA 1
@d MEMORY_USAGE_DA 2
@d TEXT_FILES_DA 3

@d NO_INLIB_DAS 4

@d MAX_DAS 100

@c
int das_created = FALSE;
debugging_aspect the_debugging_aspects[MAX_DAS];

void Log::declare_aspect(int a, wchar_t *name, int def, int alt) {
	if (das_created == FALSE) {
		das_created = TRUE;
		@<Empty the aspects table@>;
	}
	if ((a < 0) || (a >= MAX_DAS)) internal_error("aspect out of range");
	
	debugging_aspect *da = &(the_debugging_aspects[a]);
	@<Set up the new aspect@>;
}

@

@<Empty the aspects table@> =
	for (int a=0; a<MAX_DAS; a++) {
		debugging_aspect *da = &(the_debugging_aspects[a]);
		da->hyphenated_name = Str::new();
		da->unhyphenated_name = Str::new();
		da->negated_name = Str::new();
		da->on_or_off = FALSE;
		da->alternate = FALSE;	
	}

@

@<Set up the new aspect@> =
	WRITE_TO(da->negated_name, "no-");
	for (int i=0; name[i]; i++) {
		wchar_t c = name[i];
		PUT_TO(da->unhyphenated_name, c);
		if (Characters::is_space_or_tab(c)) c = '-';
		PUT_TO(da->hyphenated_name, c);
		PUT_TO(da->negated_name, c);
	}
	da->on_or_off = def;
	da->alternate = alt;

@ The debugging log provides an opportunity to see what has been happening
behind the scenes; but such a log file is often buffered by the filing system,
so that a sudden crash of NI may result in the loss of recent data written to
the log. Which is a pity, since this is exactly the most useful evidence as to
the cause of the crash in the first place. Accordingly, we fairly often
|fflush| the debug log file, forcing any buffered output to be written.

In this rest of this section, we always assume that |DL| is open. Note that it
is possible this has been switched to be |stdout|, or even that it is
temporarily the sentence tracing file: but we don't care.

@c
filename *debug_log_filename = NULL;

filename *Log::get_debug_log_filename(void) {
	return debug_log_filename;
}

void Log::set_debug_log_filename(filename *F) {
	debug_log_filename = F;
}

int Log::open(void) {
	if ((debug_log_filename) && (DL == NULL)) {
		if (STREAM_OPEN_TO_FILE(debug_log_file, debug_log_filename, ISO_ENC) == FALSE)
			return FALSE;
		DL = debug_log_file;
		Streams::enable_debugging(DL);
		LOG("Debugging log of %s\n", INTOOL_NAME);
		return TRUE;
	}
	return FALSE;
}

void Log::close(void) {
	if (DL) {
		Log::show_debugging_contents();
		STREAM_CLOSE(debug_log_file); DL = NULL;
	}
}

@p Subheadings.
To provide signposts in what is otherwise a huge amorphous pile of text,
the debugging log is divided into "phases". And phases are themselves
subdivided into "stages". This is how.

@c
char debug_log_phase[32];
int debug_log_subheading = 1;
void Log::new_phase_of_Informs_run(char *p, char *q) {
	strcpy(debug_log_phase, p);
	LOG("\n\n-----------------------------------------------------\n");
	LOG("Phase %s ... %s", p, q);
	LOG("\n-----------------------------------------------------\n\n");
	STREAM_FLUSH(DL);
	debug_log_subheading = 1;
}

void Log::new_stage_of_Informs_run(char *p) {
	LOG("\n\n==== Phase %s.%d ... %s ====\n\n", debug_log_phase, debug_log_subheading, p);
	debug_log_subheading++;
	STREAM_FLUSH(DL);
}

@p Aspects.
As mentioned above, a wide range of activities can be logged to the debugging
log: these are called "aspects" and we can switch logging of them off or on
independently. The following routine tests whether a given aspect is currently
being logged, and is used by our main macros. Aspect 0 mandates writing to the
debug log, and is used when errors occur.

@c
int Log::aspect_switched_on(int aspect) {
	int decision = the_debugging_aspects[aspect].on_or_off;
	if (aspect == DEBUGGING_LOG_INCLUSIONS_DA) decision = TRUE;
	if (decision) STREAM_FLUSH(DL);
	return decision;
}

void Log::set_aspect(int aspect, int state) {
	the_debugging_aspects[aspect].on_or_off = state;
}

@ We sometimes want to switch everything on, or switch everything off:

@c
void Log::set_all_aspects(int new_state) {
	if (DL) LOGIF(DEBUGGING_LOG_INCLUSIONS, "Set debugging aspect: everything -> %s\n",
		new_state?"TRUE":"FALSE");
	for (int a=0; a<MAX_DAS; a++) {
		debugging_aspect *da = &(the_debugging_aspects[a]);
		da->on_or_off = new_state;
	}
}

@ We also want the ability to change debugging log settings from the command
line; the command line form is derived from the textual form by replacing
every space with a hyphen: for instance, |property-provision|.

We also recognise |no-property-provision| to switch this off again,
|everything| and |nothing| with the obvious meanings, and |list| to
print out a list of debugging aspects to |STDOUT|.

@c
void Log::set_aspect_from_command_line(text_stream *name) {
	int list_mode = FALSE;
	if (Str::eq_C_string(name, L"everything")) { Log::set_all_aspects(TRUE); return; }
	if (Str::eq_C_string(name, L"nothing")) { Log::set_all_aspects(FALSE); return; }
	if (Str::eq_C_string(name, L"list")) list_mode = TRUE;

	for (int i=0; i<MAX_DAS; i++) {
		debugging_aspect *da = &(the_debugging_aspects[i]);
		if (Str::eq(name, da->negated_name)) {
			da->on_or_off = FALSE; return;
		}	
		if ((Str::eq(name, da->hyphenated_name)) ||
			(Str::eq(name, da->unhyphenated_name))) {
			da->on_or_off = TRUE; return;
		}
		if ((list_mode) && (Str::len(da->hyphenated_name) > 0)) {
			PRINT("--log=%S  (%s)\n", da->hyphenated_name, (da->on_or_off)?"on":"off");
		}
	}
	if (list_mode == FALSE)
		PRINT("No such debugging log aspect as '%S'.\n"
			"(Try running -log list for a list of the valid aspects.)\n", name);
}

@p The starred trace.
This is a useful way to switch into a more detailed view, and then switch
out again without having lost our earlier settings.

@c
void Log::tracing_on(int starred, text_stream *heading) {
	if (starred) {
		LOG("\n*** Entering sentence trace mode: %S ***\n", heading);
	} else {
		LOG("\n*** Leaving sentence trace mode: %S ***\n\n", heading);
	}
	for (int i=0; i<MAX_DAS; i++) {
		debugging_aspect *da = &(the_debugging_aspects[i]);
		int j = da->on_or_off;
		da->on_or_off = da->alternate;
		da->alternate = j;
	}
}

@p Wrapping up.
At the end of the debugging log we list what was in it, mostly to provide
the reader with a list of other things which could have been put into it,
but weren't.

@c
void Log::show_debugging_settings_with_state(int state) {
	int c = 0;
	for (int i=0; i<MAX_DAS; i++) {
		debugging_aspect *da = &(the_debugging_aspects[i]);
		if (da->on_or_off == state) {
			c++;
			LOG("  %S", da->hyphenated_name);
			if (c % 6 == 0) LOG("\n");
		}
	}
	if (c == 0) { LOG("  (nothing)\n"); } else { LOG("\n"); }
}

void Log::show_debugging_contents(void) {
	if (Log::aspect_switched_on(DEBUGGING_LOG_INCLUSIONS_DA) == FALSE) return;
	LOG("\n\nThat concludes the debugging log from this run.\n"
		"Its contents were as follows -\n\n");
	LOG("Included:\n"); Log::show_debugging_settings_with_state(TRUE);
	LOG("Omitted:\n"); Log::show_debugging_settings_with_state(FALSE);
}
