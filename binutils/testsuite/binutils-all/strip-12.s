	.section	.bss
	.space 8
	.section	.debug_str,"MS",%progbits,1
	.string	""
	.section        .text.foo,"axG",%progbits,foo,comdat
	.byte 0
