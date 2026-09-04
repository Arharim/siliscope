ruleset
=======

Canonical checks for siliscope. Own ids and wording.

	rules/*.yaml		one file per topic
	profiles/*.yaml		embedded-c, embedded-cpp, strict, style
	INDEX.md		generated id table
	coverage.md		idea → ss.* (not a standard mapping)
	schema.md		YAML fields

Id
--

	ss.<topic>.<name>

	ss.proc  ss.ctrl  ss.fn   ss.mem  ss.type  ss.conv  ss.expr
	ss.decl  ss.pre   ss.libc ss.ptr  ss.emb   ss.conc  ss.cpp
	ss.style

Profiles
--------

	embedded-c	default firmware C
	embedded-cpp	same, no EH / RTTI / heap STL
	strict		embedded-c plus noisy advisory
	style		whitespace / naming only

	default: "on" in a rule → on in embedded-c unless excluded.

Policy
------

	heap after init, recursion, setjmp	error
	goto					off except documented cleanup
	union, function pointer			advisory (HAL)
	single function exit			off
	pointer indirection			max 2
	ISR names *_isr				style only (CMSIS uses *_IRQHandler)

Adding a rule
-------------

1. Check INDEX.md for a duplicate idea.
2. Add YAML per schema.md. Own text only.
3. Enable in a profile or leave default "off".
4. python tools/validate_ruleset.py
5. python tools/generate_ruleset_index.py
