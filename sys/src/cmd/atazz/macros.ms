.de F1
.nr OI \\n(.iu
.nr PW 1v
.KF
.sp 0.3v
..
.de T1
.F1
..
.de F2
.ds Fp Figure\ \\n(Fi
.ds Fn Figure\ \\n+(Fi
.ds Fq \\*(Fp
.F0
..
.de T2
.ds Tp Table\ \\n(Ti
.ds Tn Table\ \\n+(Ti
.ds Tq \\*(Tp
.T0
..
.de F0
.nr BD 1
.if t .ps \\n(PS-1
.ie \\n(VS>=41 .vs \\n(VSu-1p
.el .vs \\n(VSp-1p
.ft 1
.di DD
.ll \\n(.lu*3u/4u
.in 0
.fi
.ad b
.sp 0.5v
\f3\\*(Fq\f1\ \ \c
..
.de T0
.nr BD 1
.if t .ps \\n(PS-1
.ie \\n(VS>=41 .vs \\n(VSu-1p
.el .vs \\n(VSp-1p
.ft 1
.di DD
.ll \\n(.lu*3u/4u
.in 0
.fi
.ad b
.sp 0.5v
\f3\\*(Tq\f1\ \ \c
..
.de F3
.sp 0.5v
.di
.br
.ll \\n(.lu*4u/3u
.if \\n(dl>\\n(BD .nr BD \\n(dl
.if \\n(BD<\\n(.l .in (\\n(.lu-\\n(BDu)/2u
.nf
.DD
.in \\n(OIu
.nr BD 0
.fi
.KE
.ie \\n(VS>=41 .vs \\n(VSu
.el .vs \\n(VSp
..
.de T3
.F3
..
.de EX
.P1
\s-4
..
.de EE
\s+4
.P2
..
.nr Fi 1 +1
.nr Ti 1 +1
.ds Fn Figure\ \\n(Fi
.ds Tn Table\ \\n(Ti
.nr XP 2	\" delta point size for program
.nr XV 2p	\" delta vertical for programs
.nr XT 4	\" delta tab stop for programs
.nr DV .5v	\" space before start of program
.FP lucidasans
.nr PS 11
.nr VS 13
.nr LL 6.6i
.nr PI 0	\" paragraph indent
.nr PD 4p	\" extra space between paragraphs
.pl 11i
.rm CH
