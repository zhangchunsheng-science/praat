writeInfoLine: "Testing runSubprocess..."
@test: empty$# (0)
@test: { "hello" }
@test: { “hello” }
@test: { """hello""" }
@test: { "a""hello""b" }
@test: { "a""""hello""""b" }
@test: { “he"llo"a” }
@test: { “he " llo"a” }
@test: { “he " llo” }
@test: { "" }
@test: { "", "" }
@test: { "hello", "" }
@test: { "", "hello" }
@test: { "hello world", "" }
@test: { "", "hello world" }
@test: { "hello world" }
@test: { "hello, world" }
@test: { "hello", "world" }
@test: { "hello goodbye", "world" }
@test: { "hello/goodbye", "world" }
@test: { "hello@goodbye", "world" }
@test: { "hello", "goodbye world" }
@test: { "hello all", "goodbye world" }
@test: { “hello goodbye”, "world" }
@test: { “hello" goodbye”, "world" }
@test: { “hello " goodbye”, "world" }
@test: { “hello "goodbye”, "world" }
@test: { “hello "goodbye"”, "world" }
@test: { “hello "goodbye" all”, "world 1a" }
@test: { “hello "goodbye"all”, "world 1b" }
@test: { “hello"goodbye" all”, "world 1c" }
@test: { “hello"goodbye"all”, "world 1c" }
@test: { “hello ""goodbye""”, "world 2" }
@test: { “hello ""goodbye"" all”, "world 3" }
@test: { “hello ""goodbye""all”, "world 4" }
@test: { “hello """goodbye""" all”, "world" }
@test: { “hello ""goodbye""allp”, "world" }
@test: { “hello ""a""""goodbye""""b""”, "world 5" }
@test: { “hello "a"""goodbye"""b"”, "world 6" }
@test: { “hello "a"""goodbye""b"”, "world 7" }
@test: { "hello goodbye", "wor ld&world" }
@test: { "hello goodbye", "&world" }
@test: { "hello&", "goodbye world" }
@test: { "hello, goodbye, world!" }

procedure test: .args$#
	@testje: "spit_win.exe", .args$#
	;@testje: "spit win.exe", .args$#
endproc
procedure testje: .spittingApp$, .args$#
	appendInfoLine: .spittingApp$, newline$, vertical$: .args$#
	.narg = size (.args$#)
	if .narg = 0
		.output$ = runSubprocess$: .spittingApp$
	elif .narg = 1
		.output$ = runSubprocess$: .spittingApp$, .args$# [1]
	elif .narg = 2
		.output$ = runSubprocess$: .spittingApp$, .args$# [1], .args$# [2]
	elif .narg = 3
		.output$ = runSubprocess$: .spittingApp$, .args$# [1], .args$# [2], .args$# [3]
	elif .narg = 4
		.output$ = runSubprocess$: .spittingApp$, .args$# [1], .args$# [2], .args$# [3], .args$# [4]
	elif .narg = 5
		.output$ = runSubprocess$: .spittingApp$, .args$# [1], .args$# [2], .args$# [3], .args$# [4],
		... .args$# [5]
	elif .narg = 6
		.output$ = runSubprocess$: .spittingApp$, .args$# [1], .args$# [2], .args$# [3], .args$# [4]
		... .args$# [5], .args$# [6]
	elif .narg = 7
		.output$ = runSubprocess$: .spittingApp$, .args$# [1], .args$# [2], .args$# [3], .args$# [4]
		... .args$# [5], .args$# [6], .args$# [7]
	elif .narg = 8
		.output$ = runSubprocess$: .spittingApp$, .args$# [1], .args$# [2], .args$# [3], .args$# [4]
		... .args$# [5], .args$# [6], .args$# [7], .args$# [8]
	elif .narg = 9
		.output$ = runSubprocess$: .spittingApp$, .args$# [1], .args$# [2], .args$# [3], .args$# [4]
		... .args$# [5], .args$# [6], .args$# [7], .args$# [8], .args$# [9]
	endif
	assert index (.output$, "arg [0] = <<" + .spittingApp$ + ">>")   ; <<'.output$'>>
	for .iarg to .narg
		assert index (.output$, "arg [" + string$ (.iarg) + "] = <<" + .args$# [.iarg] + ">>")   ; <<'.output$'>>
	endfor
	assert not index (.output$, "arg [" + string$ (.narg + 1) + "]")   ; <<'.output$'>>
endproc
appendInfoLine: "OK"
