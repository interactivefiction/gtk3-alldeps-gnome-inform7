[Regexp::] Pattern Matching.

@Purpose: To provide a limited regular-expression parser.

@p Character types.
We will define white space as spaces and tabs only, since the various kinds
of line terminator will always be stripped out before this is applied.

@c
int Regexp::white_space(int c) {
	if ((c == ' ') || (c == '\t')) return TRUE;
	return FALSE;
}

@ The presence of |:| here is perhaps a bit surprising, since it's illegal in
C and has other meanings in other languages, but it's legal in C-for-Inform
identifiers.

@c
int Regexp::identifier_char(int c) {
	if ((c == '_') || (c == ':') ||
		((c >= 'A') && (c <= 'Z')) ||
		((c >= 'a') && (c <= 'z')) ||
		((c >= '0') && (c <= '9'))) return TRUE;
	return FALSE;
}

@p Simple parsing.
The following finds the earliest minimal-length substring of a string,
delimited by two pairs of characters: for example, |<<| and |>>|. This could
easily be done as a regular expression using |Regexp::match|, but the routine
here is much quicker.

@c
int Regexp::find_expansion(text_stream *text, wchar_t on1, wchar_t on2,
	wchar_t off1, wchar_t off2, int *len) {
	for (int i = 0; i < Str::len(text); i++)
		if ((Str::get_at(text, i) == on1) && (Str::get_at(text, i+1) == on2)) {
			for (int j=i+2; j < Str::len(text); j++)
				if ((Str::get_at(text, j) == off1) && (Str::get_at(text, j+1) == off2)) {
					*len = j+2-i;
					return i;
				}
		}
	return -1;
}

@ Still more simply:

@c
int Regexp::find_open_brace(text_stream *text) {
	for (int i=0; i < Str::len(text); i++)
		if (Str::get_at(text, i) == '{')
			return i;
	return -1;
}

@ Note that we count the empty string as being white space. Again, this is
equivalent to |Regexp::match(p, " *")|, but much faster.

@c
int Regexp::string_is_white_space(text_stream *text) {
	LOOP_THROUGH_TEXT(P, text)
		if (Regexp::white_space(Str::get(P)) == FALSE)
			return FALSE;
	return TRUE;
}

@p A Worse PCRE.
I originally wanted to call the function in this section |a_better_sscanf|, then
thought perhaps |a_worse_PCRE| would be more true. (PCRE is Philip Hazel's superb
C implementation of regular-expression parsing, but I didn't need its full strength,
and I didn't want to complicate the build process by linking to it.)

This is a very minimal regular expression parser, simply for convenience of parsing
short texts against particularly simple patterns. For example:

	|"fish (%d+) ([a-zA-Z_][a-zA-Z0-9_]*) *"|

matches the word fish, then any amount of whitespace, then a string of digits
which are copied into |mr->match_texts[0]|, then whitespace again, and then an
alphanumeric identifier to be copied into |mr->match_texts[1]|, and finally optional
whitespace. If no match is made, the contents of the found strings are undefined.

@d MAX_BRACKETED_SUBEXPRESSIONS 4 /* up to four bracketed subexpressions can be extracted */

@ The internal state of the matcher is stored as follows:

@c
typedef struct X_match_position {
	int tpos; /* position within text being matched */
	int ppos; /* position within pattern */
	int bc; /* count of bracketed subexpressions so far begun */
	int bl; /* bracket indentation level */
	int bracket_nesting[MAX_BRACKETED_SUBEXPRESSIONS]; /* which subexpression numbers (0, 1, 2, 3) correspond to which nesting */
	int brackets_start[MAX_BRACKETED_SUBEXPRESSIONS], brackets_end[MAX_BRACKETED_SUBEXPRESSIONS]; /* positions in text being matched, inclusive */
} X_match_position;

@

@c
typedef struct match_result {
	wchar_t match_text_storage[64];
	struct text_stream match_text_struct;
} match_result;
typedef struct match_results {
	int no_matched_texts;
	struct match_result exp_storage[MAX_BRACKETED_SUBEXPRESSIONS];
	struct text_stream *exp[MAX_BRACKETED_SUBEXPRESSIONS];
} match_results;

@

@c
match_results Regexp::create_mr(void) {
	match_results mr;
	mr.no_matched_texts = 0;
	for (int i=0; i<MAX_BRACKETED_SUBEXPRESSIONS; i++) mr.exp[i] = NULL;
	return mr;
}

int Regexp::match(match_results *mr, text_stream *text, wchar_t *pattern) {
	mr->no_matched_texts = 0;
	for (int i=0; i<MAX_BRACKETED_SUBEXPRESSIONS; i++) {
		mr->exp_storage[i].match_text_struct =
			Streams::new_buffer(64, mr->exp_storage[i].match_text_storage);
		mr->exp[i] = &(mr->exp_storage[i].match_text_struct);
	}
	int rv = Regexp::match_r(mr, text, pattern, NULL);
	if (rv == FALSE) Regexp::dispose_of(mr);
	return rv;
}

void Regexp::dispose_of(match_results *mr) {
	for (int i=0; i<MAX_BRACKETED_SUBEXPRESSIONS; i++)
		if (mr->exp[i]) {
			STREAM_CLOSE(mr->exp[i]);
			mr->exp[i] = NULL;
		}
	mr->no_matched_texts = 0;
}

@

@c
int Regexp::match_r(match_results *mr, text_stream *text, wchar_t *pattern, match_position *scan_from) {
	match_position at;
	if (scan_from) at = *scan_from;
	else { at.tpos = 0; at.ppos = 0; at.bc = 0; at.bl = 0; }

	while ((Str::get_at(text, at.tpos)) || (pattern[at.ppos])) {
		@<Parentheses in the match pattern set up substrings to extract@>;

		int chcl, /* what class of characters to match: a |*_CLASS| value */
			range_from, range_to, /* for |LITERAL_CLASS| only */
			reverse = FALSE; /* require a non-match rather than a match */
		@<Extract the character class to match from the pattern@>;

		int rep_from = 1, rep_to = 1; /* minimum and maximum number of repetitions */
		int greedy = TRUE; /* go for a maximal-length match if possible */
		@<Extract repetition markers from the pattern@>;

		int reps = 0;
		@<Count how many repetitions can be made here@>;
		if (reps < rep_from) return FALSE;

		/* we can now accept anything from |rep_from| to |reps| repetitions */
		if (rep_from == reps) { at.tpos += reps; continue; }
		@<Try all possible match lengths until we find a match@>;

		/* no match length worked, so no match */
		return FALSE;
	}
	@<Copy the bracketed texts found into the global strings@>;
	return TRUE;
}

@

@<Parentheses in the match pattern set up substrings to extract@> =
	if (pattern[at.ppos] == '(') {
		if (at.bl < MAX_BRACKETED_SUBEXPRESSIONS) at.bracket_nesting[at.bl] = -1;
		if (at.bc < MAX_BRACKETED_SUBEXPRESSIONS) {
			at.bracket_nesting[at.bl] = at.bc;
			at.brackets_start[at.bc] = at.tpos; at.brackets_end[at.bc] = -1;
		}
		at.bl++; at.bc++; at.ppos++;
		continue;
	}
	if (pattern[at.ppos] == ')') {
		at.bl--;
		if ((at.bl >= 0) && (at.bl < MAX_BRACKETED_SUBEXPRESSIONS) && (at.bracket_nesting[at.bl] >= 0))
			at.brackets_end[at.bracket_nesting[at.bl]] = at.tpos-1;
		at.ppos++;
		continue;
	}

@

@<Extract the character class to match from the pattern@> =
	int len;
	chcl = Regexp::get_cclass(pattern, at.ppos, &len, &range_from, &range_to, &reverse);
	at.ppos += len;

@ This is standard regular-expression notation, except that I haven't bothered
to implement numeric repetition counts, which we won't need:

@<Extract repetition markers from the pattern@> =
	if (chcl == WHITESPACE_CLASS) {
		rep_from = 1; rep_to = Str::len(text)-at.tpos;
	}
	if (pattern[at.ppos] == '+') {
		rep_from = 1; rep_to = Str::len(text)-at.tpos; at.ppos++;
	} else if (pattern[at.ppos] == '*') {
		rep_from = 0; rep_to = Str::len(text)-at.tpos; at.ppos++;
	}
	if (pattern[at.ppos] == '?') { greedy = FALSE; at.ppos++; }

@

@<Count how many repetitions can be made here@> =
	for (reps = 0; ((Str::get_at(text, at.tpos+reps)) && (reps <= rep_to)); reps++)
		if (Regexp::test_cclass(Str::get_at(text, at.tpos+reps), chcl,
			range_from, range_to, pattern, reverse) == FALSE)
			break;

@

@<Try all possible match lengths until we find a match@> =
	int from = rep_from, to = reps, dj = 1, from_tpos = at.tpos;
	if (greedy) { from = reps; to = rep_from; dj = -1; }
	for (int j = from; j != to+dj; j += dj) {
		at.tpos = from_tpos + j;
		if (Regexp::match_r(mr, text, pattern, &at))
			return TRUE;
	}

@

@<Copy the bracketed texts found into the global strings@> =
	for (int i=0; i<at.bc; i++) {
		Str::clear(mr->exp[i]);
		for (int j = at.brackets_start[i]; j <= at.brackets_end[i]; j++)
			PUT_TO(mr->exp[i], Str::get_at(text, j));
	}
	mr->no_matched_texts = at.bc;

@ So then: most characters in the pattern are taken literally (if the pattern
says |q|, the only match is with a lower-case letter "q"), except that:

(a) a space means "one or more characters of white space";
(b) |%d| means any decimal digit;
(c) |%c| means any character at all;
(d) |%C| means any character which isn't white space;
(e) |%i| means any character from the identifier class (see above);
(f) |%p| means any character which can be used in the name of a Preform
nonterminal, which is to say, an identifier character or a hyphen;
(g) |%P| means the same or else a colon.
(h) |%t| means a tab.

|%| otherwise makes a literal escape; a space means any whitespace character;
square brackets enclose literal alternatives, and note as usual with grep
engines that |[]xyz]| is legal and makes a set of four possibilities, the
first of which is a literal close square; within a set, a hyphen makes a
character range; an initial |^| negates the result; and otherwise everything
is literal.

@d ANY_CLASS 1
@d DIGIT_CLASS 2
@d WHITESPACE_CLASS 3
@d NONWHITESPACE_CLASS 4
@d IDENTIFIER_CLASS 5
@d PREFORM_CLASS 6
@d PREFORMC_CLASS 7
@d LITERAL_CLASS 8
@d TAB_CLASS 9

@c
int Regexp::get_cclass(wchar_t *pattern, int ppos, int *len, int *from, int *to, int *reverse) {
	if (pattern[ppos] == '^') { ppos++; *reverse = TRUE; } else { *reverse = FALSE; }
	switch (pattern[ppos]) {
		case '%':
			ppos++;
			*len = 2;
			switch (pattern[ppos]) {
				case 'd': return DIGIT_CLASS;
				case 'c': return ANY_CLASS;
				case 'C': return NONWHITESPACE_CLASS;
				case 'i': return IDENTIFIER_CLASS;
				case 'p': return PREFORM_CLASS;
				case 'P': return PREFORMC_CLASS;
				case 't': return TAB_CLASS;
			}
			*from = ppos; *to = ppos; return LITERAL_CLASS;
		case '[':
			*from = ppos+2;
			while ((pattern[ppos]) && (pattern[ppos] != ']')) ppos++;
			*to = ppos - 1; *len = ppos - *from + 1;
			return LITERAL_CLASS;
		case ' ':
			*len = 1; return WHITESPACE_CLASS;
	}
	*len = 1; *from = ppos; *to = ppos; return LITERAL_CLASS;				
}

@

@c
int Regexp::test_cclass(int c, int chcl, int range_from, int range_to, wchar_t *drawn_from, int reverse) {
	int match = FALSE;
	switch (chcl) {
		case ANY_CLASS: if (c) match = TRUE; break;
		case DIGIT_CLASS: if (isdigit(c)) match = TRUE; break;
		case WHITESPACE_CLASS: if (ISORegexp::white_space(c)) match = TRUE; break;
		case TAB_CLASS: if (c == '\t') match = TRUE; break;
		case NONWHITESPACE_CLASS: if (!(ISORegexp::white_space(c))) match = TRUE; break;
		case IDENTIFIER_CLASS: if (Regexp::identifier_char(c)) match = TRUE; break;
		case PREFORM_CLASS: if ((c == '-') || (c == '_') ||
			((c >= 'a') && (c <= 'z')) ||
			((c >= '0') && (c <= '9'))) match = TRUE; break;
		case PREFORMC_CLASS: if ((c == '-') || (c == '_') || (c == ':') ||
			((c >= 'a') && (c <= 'z')) ||
			((c >= '0') && (c <= '9'))) match = TRUE; break;
		case LITERAL_CLASS:
			for (int j = range_from; j <= range_to; j++) {
				int c1 = drawn_from[j], c2 = c1;
				if ((j+1 < range_to) && (drawn_from[j+1] == '-')) { c2 = drawn_from[j+2]; j += 2; }
				if ((c >= c1) && (c <= c2)) {
					match = TRUE; break;
				}
			}
			break;
	}
	if (reverse) match = (match)?FALSE:TRUE;
	return match;
}
