\ title:  Hello Forth
\ author: TIC-80
\ desc:   Forth demo - change the word called in TIC to switch example
\ license: MIT License
\ script: forth
\
\ -- TIC-80 Forth API --------------------------------------------------
\ Drawing
\   CLS   ( color -- )
\   PIX!  ( x y color -- )        set pixel
\   PIX   ( x y -- color )        get pixel
\   LINE  ( x0 y0 x1 y1 color -- )
\   RECT  ( x y w h color -- )    filled rectangle
\   RECTB ( x y w h color -- )    rectangle border
\   CIRC  ( x y r color -- )      filled circle
\   CIRCB ( x y r color -- )      circle border
\   ELLI  ( x y a b color -- )    filled ellipse
\   ELLIB ( x y a b color -- )    ellipse border
\   TRI   ( x1 y1 x2 y2 x3 y3 color -- )  filled triangle
\   TRIB  ( x1 y1 x2 y2 x3 y3 color -- )  triangle border
\   CLIP  ( x y w h -- )    CLIP0 ( -- )  reset clip region
\ Text
\   PRINT ( c-addr u x y color fixed scale alt -- width )
\   FONT  ( c-addr u x y chroma cw ch fixed scale alt -- width )
\   TRACE ( c-addr u color -- )
\ Sprites & map
\   SPR   ( id x y colorkey scale flip rotate w h -- )
\   MAP   ( cellx celly cellw cellh sx sy colorkey scale -- )
\   MGET  ( x y -- tile )    MSET  ( x y tile -- )
\   FGET  ( id flag -- bool )  FSET ( id flag val -- )
\ Input
\   BTN   ( id -- pressed )   ids: 0=up 1=down 2=left 3=right 4=A 5=B
\   BTNP  ( id hold period -- pressed )
\   KEY   ( code -- pressed )  KEYP ( code hold period -- pressed )
\   MOUSE ( -- x y left mid right scrollx scrolly )
\ Sound
\   SFX   ( id note octave dur chan vol speed -- )
\   MUSIC ( track frame row loop sustain tempo speed -- )
\ Memory
\   PEEK  ( addr bits -- val )   POKE  ( addr val bits -- )
\   PEEK1 ( addr -- val )        POKE1 ( addr val -- )
\   PEEK2 ( addr -- val )        POKE2 ( addr val -- )
\   PEEK4 ( addr -- val )        POKE4 ( addr val -- )
\   MEMCPY ( dst src size -- )   MEMSET ( dst val size -- )
\   PMEM  ( index -- val )       PMEM! ( val index -- )
\ Misc
\   TIME    ( -- ms )    TSTAMP ( -- sec )
\   VBANK   ( bank -- prev )
\   SYNC    ( mask bank tocart -- )
\   EXIT    ( -- )       RESET  ( -- )
\ String helper (defined below)
\   n>str  ( n -- c-addr u )
\ ---------------------------------------------------------------------

VARIABLE n     \ frame counter
VARIABLE px    \ sprite x
VARIABLE py    \ sprite y

\ ( n -- c-addr u )  integer to string
: n>str  S>D <# #S #> ;

\ ( c-addr u x y -- )  print white at position, scale 1
: at  15 FALSE 1 FALSE PRINT DROP ;

: BOOT
  96 px !  24 py !
  S" Forth BOOT" 12 TRACE
;

\ -- Demo 1 : shapes --------------------------------------------------
: demo01
  0 CLS
  0 20 239 20 6 LINE
  120 0 120 135 6 LINE
  10 30 70 35 2 RECT
  90 30 70 35 3 RECTB
  55 95 20 4 CIRC
  55 95 20 5 CIRCB
  170 95 45 18 9 ELLI
  170 95 45 18 6 ELLIB
  10 75 70 75 40 55 8 TRI
  90 75 150 75 120 55 7 TRIB
  n @ 200 MOD 30 +  40  15 PIX!
  S" LINE"  4  12 at
  S" RECT"  10 68 at
  S" RECTB" 90 68 at
  S" CIRC"  36 118 at
  S" ELLI"  145 118 at
  S" TRI"   24 78 at
  S" TRIB"  100 78 at
;

\ -- Demo 2 : sprite + arrow keys -------------------------------------
: demo02
  0 BTN IF  py @ 1- py ! THEN
  1 BTN IF  py @ 1+ py ! THEN
  2 BTN IF  px @ 1- px ! THEN
  3 BTN IF  px @ 1+ px ! THEN
  13 CLS
  1 px @ py @ 14 3 0 0 2 2 SPR
  S" Hello Forth!" 84 84 15 FALSE 1 FALSE PRINT DROP
;

\ -- Demo 3 : frame counter -------------------------------------------
: demo03
  0 CLS
  S" Hello, Forth!" 84 48 15 FALSE 2 FALSE PRINT DROP
  n @ n>str 116 92 14 FALSE 1 FALSE PRINT DROP
;

\ -- Change the word called here to switch demo -----------------------
: TIC
  \ demo01
   demo02
  \ demo03
  n @ 1+ n !
;


\ <TILES>
\ 001:eccccccccc888888caaaaaaaca888888cacccccccacc0ccccacc0ccccacc0ccc
\ 002:ccccceee8888cceeaaaa0cee888a0ceeccca0ccc0cca0c0c0cca0c0c0cca0c0c
\ 003:eccccccccc888888caaaaaaaca888888cacccccccacccccccacc0ccccacc0ccc
\ 004:ccccceee8888cceeaaaa0cee888a0ceeccca0cccccca0c0c0cca0c0c0cca0c0c
\ 017:cacccccccaaaaaaacaaacaaacaaaaccccaaaaaaac8888888cc000cccecccccec
\ 018:ccca00ccaaaa0ccecaaa0ceeaaaa0ceeaaaa0cee8888ccee000cceeecccceeee
\ 019:cacccccccaaaaaaacaaacaaacaaaaccccaaaaaaac8888888cc000cccecccccec
\ 020:ccca00ccaaaa0ccecaaa0ceeaaaa0ceeaaaa0cee8888ccee000cceeecccceeee
\ </TILES>

\ <WAVES>
\ 000:00000000ffffffff00000000ffffffff
\ 001:0123456789abcdeffedcba9876543210
\ 002:0123456789abcdef0123456789abcdef
\ </WAVES>

\ <SFX>
\ 000:000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000304000000000
\ </SFX>

\ <PALETTE>
\ 000:1a1c2c5d275db13e53ef7d57ffcd75a7f07038b76425717929366f3b5dc941a6f673eff7f4f4f494b0c2566c86333c57
\ </PALETTE>

\ <TRACKS>
\ 000:100000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000
\ </TRACKS>

