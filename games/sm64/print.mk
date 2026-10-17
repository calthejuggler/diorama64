# Used as: gmake -f Makefile -f print.mk print-VAR
print-%: ; @echo '$($*)'
