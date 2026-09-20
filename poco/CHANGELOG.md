Changes made in each Poco version...
  (This file is the whole history; it absorbed the DOS-era versions.doc and
   versnold.doc, which have been deleted.)

 Version    Date    Changes

    100   07/??/90  Baseline code delivered to Ian.

    106   08/21/90  Big round of commenting and general cleanup.

    107   08/23/90  Jim made small integration-related changes.

    108   08/24/90  Added expression frame caching.

    109   08/26/90  Start of new memory management changes.  In this round,
                    all occurrances of freemem() were changed to poc_freemem(),
                    and the basics were coded in poco.c to implement an alloc-
                    only memory management system.   Also, a bunch of code
                    in linklist.c was commented out to save space, and a
                    change was made so that line data is not added when the
                    line number is zero (to save transient memory).

    110   08/27/90  Second round of memory management changes.  All MM-related
                    routines were consolidated in POCMEMRY.C (formerly in
                    POCO.C).  Also, a generic structure caching system was
                    built, that works in conjunction with the MM routines, for
                    caching code buffers, poco_frames, etc.  At this point,
                    nothing is using this new common struct caching, but the
                    framework is in place.

    111   08/28/90  Third round of mm changes.  Added calls to poc_begcache()
                    to implement caching for exp_frames, code_bufs, and
                    poco_frames.

    112   08/29/90  More mm changes.  The code_buf caching was changed to a
                    more generic 'small block' caching (only in the sense of
                    the way things are named, not how they work.)  Then,
                    new_line_data() was changed to obtain the initial data
                    area from the small block cache.  (IE, now the 16-slot
                    cache of 512-byte blocks serves both code buffers and
                    line data areas.)

    113   08/29/90  Start of setjmp/longjmp error handling.  Moved some cleanup
                    functions from compile_file() to compile_poco().  Also
                    removed a lot of DEADWOOD code.  Removed needless retval
                    checking in POCMEMRY.C.

    113B  08/29/90  Continued removing retval checking, then wimped out and
                    backed off this whole version.  (IE, this is essentially
                    a non-version, but I backed it up anyway.)  Nothing
                    actually broke, it just felt wrong.

    114   08/31/90  Fixed bop_expression() to handle pointer arithmetic. Also
                    a couple very minor changes to other modules (removed the
                    pf.cache stuff in poco that was left over, etc.)  Basically,
                    the only real changes to this release are a new bop.c
                    module, and the code_elsize() routine in poco.c was made
                    globally visible.  If a situation comes up where the MM
                    or setjmp stuff has to be backed out, the bop.c module
                    can be extracted from this release and applied to the
                    backed-out version without trouble.

    115   08/31/90  Jim fixed pocmemry and linklist to keep Watcom happy.

    116   08/31/90  Jim fixed pocoface to close the err_file on compile exit.
                    Also changed file open mode for source files to "rb".

    117   09/01/90  Combined reverse_comps() and reverse_dims() into a single
                    routine.  Nuked check_rparen().  Tweaked get_sprim().
                    Tweaked jgets().  Undid "rb" open mode in pp.  Added
                    command-line switch processing to main().

    117A  09/03/90  Another non-version.  This was the first attempt at
                    rewriting the preprocessor and front end in general.

    118   09/04/90  Major tweaks to prep_line() in PP.C.

    119   09/05/90  Completion of the prep_line() tweaks (basically, it was
                    rewritten).  Added pp_define() to handle #define
                    lines for the new prep_line().  Also, added new cmdlin
                    switch '-v' to main() -- prints version number.  Version
                    number comes from the version variable in the makefile
                    via a -DVERSION=$(VERSION) switch on the compiler cmdlin
                    in the makefile.

    120   09/06/90  Added support for ANSI builtin macros (__TIME__, etc).
                    Other misc cleanup in pp.c, getting ready for #if.

    121   09/07/90  Added support for #if, cleaned up some other little bugs
                    in pp.  Also, fixed a bug in jgets() that was causing the
                    quote-matching logic to be applied inside of comment
                    blocks.  And, tweaked htol() to handle the leading 0x on
                    a hex string, if it exists.  (New module PPEVAL.C was
                    added with this version.)

    122   09/08/90  Added support for #elif, and detection of infinite loops
                    in macro substitution.

    123   09/09/90  Fixed include path in main.c for Jim as well as Ian.
                    Also added a NULL check in next_token routine in ppeval.c
                    to avoid memory protection crash in 386 mode.

    124   09/18/90  Added support for unions, fixed a bug in handling of
                    nested structures in which the inner structure was not
                    accessible.  (The inner structure definition now gets
                    copied to the parent (func or global) frame.) Tweaked
                    error messages in not_a_member() to distinguish between
                    an incompletely defined structure and a reference to a
                    member not in a fully defined structure.

    125   09/19/90  Added support for all ANSI type-related keywords (const,
                    auto, static, etc), mostly as see-and-ignore keywords for
                    now.  Also, pretty drastically changed the way statement()
                    and get_base_type() deal with type tokens.

    126   09/19/90  Various tweaks to allow phansi test code to run....
                    > We recognize 'cdecl' as legal, if DEVELOPMENT is defined,
                      so that the TC headers can be used for testing.
                    > We allow a proto like "void main(void)".  This is not a
                      phansi-only hack, it will stay in permenantly.  (Note
                      that this currently works only for 'void', it should
                      work for all type keywords, but only in a true proto.)
                    > Fixed a preprocessor bug that was causing a number such
                      as 0x80 to be mistaken for a #define'd symbol with the
                      name x80 during macro expansion.
                    > Fixed a preprocessor bug that was causing a #define
                      of a symbol with a null value to sometimes contain
                      garbage (eg, #define xyz\n wasn't always working.)

    127   09/20/90  Fixed various bugs pointed out by phansi code...
                    > An 'L' suffix is now allowed on float constants.
                      (Changed in TOKEN.C, but *shouldn't* hurt other callers).
                    > Implemented ANSI special scoping rule for structures
                      and unions.  A definition such as "struct tagname;"
                      builds a new incomplete definition at the current scope
                      even if the tag already exists at an outer scope.
                    > Fixed a 'do forever' loop situation in pf_to_sif() that
                      would occur when struct tags were defined within nested
                      struct defintions.
                    > Added error checking for attempting to index a void
                      pointer (eg, void *a; a[2] = 3; is now illegal).  Also,
                      attempt to dereference a void pointer using '*' now has
                      its own error message, instead of reporting 'confused
                      pointer dereference'.

    128   09/21/90  > Fixed bug in RUNOPS and BOP so that the result of a
                      comparison is always an INT type.  This allows mixed
                      comparisons & booleans such as (1.0 < 2.0 && 1L < 2L)
                      to work properly (used to promote everything to double
                      then die on the && with double-type operands.)

    129   10/01/90  > Fixed a bug in CHOPPER that was causing long multiline
                      comment blocks to yield bogus 'line too long' errors.
                    > Fixed a bug involving the OP_LEAVE instruction.  The
                      instruction used to have an offset coded in it that was
                      used to clean local vars off the stack, and the offset
                      could be wrong when a function contained multiple
                      return statements.  Now the base register which already
                      existed in the runtime interpreter is used to clean
                      local vars off the stack.

    130   10/02/90  > Added support for enums.

    131   10/02/90  > Enhanced handling of sizeof() operator.  It now deals
                      with arbitrary expressions and such in a nice ANSI-
                      type manner, but it doesn't yet handle a type or
                      typedef name.

    132   10/02/90  > Did a crude extension of the sizeof() handling.  It
                      now takes sizeof(typename) where typename is any
                      integral or user-defined type, followed by any number
                      of stars.  We don't support the full ANSI concept of
                      a typename (as described in K&R2 A.8.8) yet.
                    > Fixed bug in not_a_member() that was causing a page
                      fault when the struct/union had no tag.
                    > Fixed a bug in detecting EOF inside an open #if/#ifdef
                      block.  We used to only detect it if EOF occurred while
                      out_of_it was > 0, now we always detect it.

    133   10/03/90  > Type-parsing changes, round 1.  We no longer allow
                      nonsense declarations (eg, signed unsigned long short a;)
                      This is a step towards supporting static vars (and
                      maybe someday unsigned?)  Changes were primarily in
                      get_base_type(), with a couple fields added to the
                      Type_info and Symbol structures, as well.

    134   10/04/90  > Added full support for static vars.  Big changes in
                      poco.c and declare.c, see comments therein for details.
                      Also, added enforcement of ANSI requirement for
                      constant init expressions for static/array/struct vars.

    135   10/04/90  > Tweaked say_fatal() so that it can take a format
                      string and args, like printf.  In all modules where
                      instances where a sprintf/say_fatal pair appeared,
                      they were replaced with a call to say_fatal alone.
                    > Fixed some bugs in error detection and reporting...
                    > In check_dupe_protos(), an argument mismatch was
                      being reported as a return type mismatch.
                    > In mk_func_call(), it wasn't properly reporting
                      too many/few parms, just an expecting_got msg that
                      was not as clear.  Now reports too many/not enough.

    136   10/05/90  > Added poc_say_internal() to report our internal sanity
                      check error messages with 'poco internal error:'
                      prepended to the messages.
                    > Reviewed message text for all say_fatal() and
                      expecting_got() error messages, tweaked some of them
                      for conistancy or clarity.

    137   10/07/90  > In fold.c increased size of the fold_stack from 2 to
                      16 times sizeof(double).  In code.c, reworked the
                      logic add_code() uses when deciding whether to get a
                      new code buffer from cache.

    138   10/07/90  > Removed check for NULL pointer return value everywhere a
                      memory management routine is called.

    139   10/07/90  > Removed all checks for pcb->got_stop.  This is fairly
                      safe, since nothing has *set* got_stop since the
                      setjmp/longjmp stuff was added.

    140   10/07/90  > Removed all check of pcb->global_err.

    141   10/09/90  > Did renaming of jfile family of functions to pj_file
                      to match rest of PJ.

    142   10/09/90  > More renaming of host-provided functions to pj_whatever.

    143   10/19/90  > Fixed size of short variables to be two bytes again.
                    > Fixed order-of-evaluation problem for && and || in
                      PPEVAL.C.  Also, grep'd the rest of the poco source
                      for similar problems (found none).

    144   10/20/90  > Fixed sizeof() handling, it now accepts a typename as
                      defined in ANSI/K&R2 A.8.8; in other words, full support
                      for sizeof(). (Er, except it didn't always work right,
                      and was fixed in a later version.)

    145   10/20/90  > Fixed make_deref() such that a symbol declared as an
                      array can be dereferenced via a '*'.  Fixed the routines
                      find_local_assign(), and find_global_assign() such that
                      the name of an array is not taken as an lvalue. Ex:
                        char a[5];
                        something = *a;     /* this is now legal */
                        ++a;                /* this is now illegal */

    145A  10/20/90  > A non-version that was backed up.  In this version,
                      handling for recasts was added to poco.c, then it was
                      discovered that both recasts and sizeof won't work
                      right in all cases without support for a two-token
                      lookahead.  Since that involves massive changes, this
                      version was backed off.

    145B  10/21/90  > Another non-version.  In this version, two huge sets
                      of changes were made in a pretty much hack-it-in
                      manner, just to see if the ideas would work.  This
                      version was backed off so that the two sets of changes
                      can be applied in a controlled manner, now that the
                      methods are proven.  (Changes implemented multi-token
                      lookaheads, and a rewritten interface to the builtin
                      libraries and prototypes.)

    146   10/22/90  > Reworked the interface between poco and the parent's
                      library prototypes.  The preprocessor routine
                      pp_next_line() is now the source of all input lines
                      from the parser's point of view (no more indirect calls
                      thru pcb->next_line).  All the former routines to deal
                      with proto data were replaced by poc_get_libproto_line(),
                      in POCOFACE.  Tweaks were done in POCO, PP, DECLARE, and
                      TRACE to correspond to this.  Also, some fields were
                      changed within Poco_cb in POCO.H. This paves the way for
                      loadable libraries (via #pragma) and cleans up some
                      problems with multi-token lookaheads.
                    > Fixed a bug in the OP_ICCALL instruction in RUNOPS. The
                      instruction pointer was being advanced before checking
                      for an error in the lib routine's return status, and the
                      print_trace() error reporting was wrong because of it.
                    > Fixed routines in TRACE to work under TURBO C, and
                      prettied up some of the error messages.
                    > Fixed a bug in PP that would have caused a page fault
                      upon reporting an error in #undef processing.

    147   10/23/90  > Added support for multi-token lookahead.  All occurances
                      of ts[0].whatever changed to curtoken->whatever in all
                      modules.  Major changes in POCO.C and POCO.H, see
                      comments therein for details.  Note that nothing
                      actually looks ahead yet with this change, it just sets
                      the stage.  At this point, all v143 thru v147 changes
                      were tested under Watcom.

    148   10/23/90  > Implemented full ANSI-type sizeof() handling, using
                      new lookahead facility.  (This time it works.)

    149   10/23/90  > Implemented type casting (full ANSI-style support
                      except that, by design, numbers cannot be cast to
                      pointers or vice versa).

    150   10/23/90  > Massive global name changes to prepend 'po_' onto all
                      global function names.  Changes done using subs.exe
                      utility.  Source file for the changes, subs.in, is
                      backed up in the archive for this version.

    151   10/23/90  > Some changes to main.c to get it to link with
                      PJ Watcom libraries that have further pj_ type
                      name substitutions.   Change to makefile.wc to
                      use protected mode version of compiler and to
                      produce a poco.lib file which is used instead
                      of all the .obj files when linking into PJ.
                      (Also improved ^Z handling in PJ stdio flush-read
                      routine.)

    152   10/23/90  > Poco was re-validated using phansi test code.  The only
                      fix needed was to add 'char' to the list of types that
                      can have 'signed' or 'unsigned' specified with it.
                    > Implemented pointer difference (subtraction of two
                      pointers).  This involved a major tweak to BOP.C, plus
                      the addition of a new opcode (OP_PTRDIFF) to RUNOPS.C.

    153   10/24/90  > Changes in po_types_same() and po_coerce_expression()
                      to make pointers and arrays more interchangable. (This
                      is the completion of a 'fix' done in version 145.)
                    > Fixed bug that would cause page fault when an empty
                      file was fed to poco (bad refs to file_stack->line_count
                      when file_stack was NULL due to lookahead having
                      already finished reading the file).
                    > Fixed bug in tokenize_word() that would stop in the
                      middle of a long C-symbol token, leaving the rest of
                      the token to be processed as if it were the next token
                      on the line.  Now we eat the whole token, but only
                      return MAX_SYM_LEN chars to the caller.

    154   10/25/90  > Changed handling of library prototypes - now uses the
                      file_stack structure to track which library we're in.
                    > Added #pragma poco library <xxx> handling for loadable
                      libraries (still need pj-side function for this).
                    > Added #pragma poco [macrosub|nomacrosub] to turn macro
                      substitution on and off; this can be used by libraries
                      (which start in nomacrosub mode) or user source files
                      (which start in macrosub mode).
                    > Added check for stack overflow in lookup_freshtoken().

    155   10/26/90  > Changed all curtoken->type references to t.toktype,
                      to remove a level of indirection on the most frequently
                      access token field.  Defined PoBoolean type as a byte,
                      currently only used for the t.reuse field.
                    > Fixed a glitch in parameter counting in mk_function_call.
                    > Fixed a glitch in get_pmember that allowed the wrong
                      level of indirection on ptr->member expressions.  Now
                      the type_info of the ptr must have exactly 2 comps.

    156   10/27/90  > Fixed a page fault glitch that the parm count fix made.
                    > Fixed a typo in po_types_same that was allowing type
                      mismatches.

    157   10/27/90  > Fixed glitches involved with recasting pointers.  When
                      parsing a typename, we now keep any prototypes we find
                      instead of discarding them like we used to.
                    > An integrity check was added to OP_CALLI processing in
                      RUNOPS, so that if something is recast to a function
                      pointer type, and a call is made thru that pointer,
                      RUNOPS will now verify that the pointer is pointing
                      to a function_frame, if not, it reports an error.
                    > Added handling of ANSI-type indirect function calls to
                      get_function(), such both "ptr()" and (*ptr)()" work.

    158   10/28/90  > Added concatenation of adjacent string litterals to
                      build_token_list() in POCO.C.  To keep things simple,
                      there is a limit of 4k on the total length of all
                      adjacent strings.
                    > Added "#pragma echo" command, available only when the
                      DEVELOPMENT symbol is defined.  This allows our test
                      and verification .poc programs to talk to us while they
                      are being compiled.
                    > Added new routine, translate_escape(), to TOKEN.C. It
                      replaces the old translate_escapes() routine which
                      didn't handle octal constants properly.  Also, it is no
                      longer necessary to call the translator, that process is
                      now part of tokenize_word(). This affects SOFTOK.C in PJ.
                    > Added handling of 'F' suffix on numeric constants in
                      get_digits() in TOKEN.C, per ANSI spec.
                    > Added error check for invalid character constants.  We
                      allow an empty constant (''), which has a value of zero,
                      but we disallow multi-char constants such as 'abc'.

    159   10/28/90  > Fixed a glitch which was allowing reserved words (such
                      as char or case) to be used as symbol names in non-
                      global scopes.  The cause is the way force_local_symbol
                      works, but the easier fix was in lookup_freshtoken.
                    > Tightened type conversion rules in upgrade_to_ido(). We
                      now prevent the conversion of IDO_CPT and IDO_VPT to
                      a numeric type.
                    > Added a check in array_decl() to disallow zero-dimension
                      array declarations except in function prototypes.
                    > Added global variable po_version_number in POCOFACE.C.
                      This is initialized (at compile time) to the version
                      number value that comes in from the makefile.  PJ can
                      use this number for its own evil purposes.  Also,
                      function po_version() in POCOFUNC.C should be changed
                      from return(0) to return(po_version_number).
                    > Changed get_if(), get_do(), get_while(), and get_for()
                      to allocate their conditional expression frames from
                      the cache instead on the stack.  This allows virtually
                      any level of nesting, limited only by total memory
                      instead of by PJ's stack size.

    160   10/29/90  > Once again fixed problems with interchangability of
                      array names and pointers.  This time the multiple-star
                      dereferencing problem was fixed (I hope), so that **a
                      will deref to the first element of a 2-d array, and
                      so on.  (Note to self: There is a BIG writeup in the
                      comment block in POCO.C on this situation and its
                      history of attempts, if another fix is needed.)

    161   10/29/90  > Implemented ANSI feature which allows prototypes to
                      have typenames only (no symbol names) as long as a
                      code block doesn't follow the prototype.  Changes to
                      dirdcl(), get_body(), and po_rehash().
                    > Fixed handling of string literals that have embedded
                      \0 characters in them.  Now char a[] = "a\0b"; will
                      properly allocate the array at 4 bytes.
                    > Fixed a glitch in PP.C that was causing spurious EOF
                      inside #if/#ifdel messages.
                    > Fixed a glitch in CHOPPER.C that was causing spurious
                      unterminated string errors if the char const was '\\'.

    162   10/30/90  > Fixed an over-optimization in po_pp_next_line that
                      was keeping the file stack in a local variable even
                      though embedded call to feed_pp might change it.
                      (Somehow this seemed to work anyways for #includes,
                      but died horribly in #pragma library.)
                    > Generally hacked in #pragma library.  Added some
                      fields to Poco_lib and Poco_run_env to do it.
                      Changes mostly in pocoface.c.

    163   11/28/90  > Added new module RUNCCALL.ASM and made corresponding
                      changes in RUNOPS.C.  This provides a new call glue
                      mechanism when Poco code calls C functions (library or
                      POE functions); now the C function will get control
                      with all the parms on the stack just like it had been
                      called from another C function.  (The poco_get_stack()
                      function is no longer needed to access parms.)

    164   12/11/90  > Added prototype to poco.h for po_move_sifs_to_parent().
                    > Tweaked var_init() routine in declare.c.  It now keeps
                      a list of offsets that need patching after the init of
                      an implied-dimension array (array[] = {...}), instead
                      of (incorrectly) calc'ing to each patch location.
                    > Relaxed the restriction requiring constant init values
                      for arrays and structures (but NOT for local static
                      vars).  This is a temporary workaround to the lvalue
                      pure_constant bug.
                    > Added code to MAIN.C to allow redirection of stdout
                      to a file.  This is controlled by a new command line
                      switch, -ofilename, and it will redirect _everything_
                      that normally goes to the screen into the file (from
                      the compiler and from the poco program once it begins
                      running).  This is to help support automatic testing
                      and verification of poco using the POS and NEG programs.
                      Also, nuked the old '-e' command line switch, since
                      -o does the same thing only better.
                    > Changed some occurances of printf() to fprintf(stdout)
                      throughout poco.  This was mostly stuff inside of
                      #ifdef DEVELOPMENT blocks.
                    > Added a couple extra reasonablness checks to the code
                      in pocodis.c, to prevent page faults when disassembling
                      a corrupted code buffer.

    165   12/13/90  > Changed method of allocation for poco programs' runtime
                      stack.  Stack is now alloc'd by poco_cont_ops(), which
                      allows reentry into the interpreter from a library or
                      poe routine.  (There is special case code to prevent
                      allocation for little fold stacks during compile.)
                    > Added #pragma poco stacksize to pp.c, allows program
                      to request runtime stack of 4 to 64k. (Default 10k).
                    > Added better stack overflow checking to runops, so that
                      we can guarantee library & poe routines 2k of stack.
                    > Eliminated poco_get_stack(), poco_add_param(),
                      poco_cleanup_param(), poco_param_left(), and
                      get_poco_varg(), from runops.c.  To the outside world,
                      a call from poco code to C code now looks just like a
                      normal C call, so these aren't needed anymore.
                    > Made changes to library routine in safefile, strlib,
                      etc, plus on the PJ side, to eliminate poco_get_stack()
                      and get_poco_varg() calls.

    166   12/14/90  > Fixed po_puts() in main.c.  (Added newline.)

    167   02/07/91  > Added builtin macros: Array_els, TRUE, FALSE. Did major
                      tweaking of library functions, including lots of sanity
                      checking in the safefile library.  Tweaked FUNCCALL.C
                      so that calls with variable args now include the arg count
                      and size in the stacked parms, for better sanity checking
                      at runtime.  Added module POCOUTIL.ASM with some fast asm
                      subroutines for poco's most-used routines.  Did some
                      general speed tweaking throughout poco.  All these
                      changes were in sync with tweaking done in the poco*.c
                      modules in the PJ root; this is of interest primarily
                      to the variable args function calling stuff.

    168   03/21/91  > Enhanced functionality of po_findpoe() in pocoface.c (it
                      can now walk the linked lists returning each library).
                      Enhanced the searching of the include directories
                      specified by the host (this done in sync with changes in
                      the host's qpoco.c module).  The search order is now
                      <null path>, <program's path>, <resource dir>.  (The
                      difference here is the addition of the program's path).
                      Also, POE library modules loaded via #pragma are now
                      searched for in the same way the include files are.  As
                      a side effect, these changes eliminated two bugs which
                      would have crashed pj/poco:  if a #include had a 0-length
                      name, or a mega-long name, we weren't handling it at all.

    169   04/03/91  > Fixed error in Watcom pragmas in POCO.H: sometimes
                      pragmatized functions would get called when the source
                      code hadn't seen the pragma (eg, in safefile.c), so
                      garbage would be in the regs used for parm passing.  Now
                      the pragmatized functions are covered by synonyming them
                      to __function__ in the pragma statement, so that they
                      can't get called by accident.  Also, in RUNOPS.C, changed
                      the minimum stack size on a lib/poe call from 2k to 4k.
                      And finally, fixed a bug in PP.C that was causing problems
                      with strings such as "\\"; the 2nd backslash was seen by
                      the preprocessor as 'escaping' the ending quote, and thus
                      the true end of the string would be missed, and any macros
                      following that point on the line wouldn't get
                      substituted.

    170   04/16/91  > Peter modified pp_pragma() to extend the syntax of the
                      pragma poco library statement.  It now accepts a string
                      following the library name, and if the string is present,
                      it must match (case-insensitive) the ID string coded in
                      the POE library module.  If no string is specified, the
                      ID string from the POE module is ignored.  Also, the
                      atoi() function was added to the string library
                      (strlib.c).

    171   05/01/91  > Fixed trapping of floating point errors on the host
                      side; fp errors now set builtin_err to Err_float but
                      don't directly report to the user.  Moved 'ownership'
                      of the builtin_err variable into poco.  (It used to be
                      declared on the host side, now it's declared in
                      pocoface.c).  This was done so that floating point errors
                      during constant-folding (eg, during the compile phase)
                      can be seen by poco.
                    > Added #pragma poco eof.
                    > Tweaked error reporting routine say_error in POCO.C. It
                      used to report the line number on which an error occurred
                      based on file_stack->line_count.  Because of the multi-
                      token lookahead logic, this can be as much as 5 to 10
                      lines beyond where the error happened.  Now we report the
                      line number based on curtoken->line_num, which is usually
                      the right line number (sometimes it'll be the next line
                      number, because of the vagaries of binop expression
                      parsing with the shift/reduce part of the parser).
                    > Error reporting also enhanced to report the error message
                      text (from aa.mu) if the error is more complicated than
                      a simple Err_syntax.

    172   05/21/91  > Added a check to check_dupe_proto in declare.c to prevent
                      a function supplied via the proto strings in a POE lib
                      to duplicate the name of a function in the builtin libs.

    173   07/06/91  > Fixed a glitch in the po_types_same() routine that was
                      allow bogus ambiguity between arrays and pointers. (IE,
                      it was thinking "char a[2][2]" and "char *a[2]" were
                      equivelent.)  Now for a pointer and an array to be
                      synonomous with each other, they must be a pointer-to
                      and an array-of the same type of things.  Note that
                      this is bypassed at a higher level for void pointers,
                      so "char *a; void **b; b=a;" is still legal when it
                      shouldn't be.
                    > Added a new error code, Err_poco_exit, and added some
                      logic to runops.c to go with it.  This error code is
                      used ONLY by the po_exit() library routine.  When the
                      lib routine sets this status into builtin_err, runops
                      will shut down, but will return a Success status instead
                      of Err_abort like it used to.  This makes PocoChainTo()
                      work properly when followed by 'exit(0);' in the code.
                    > Added an fputc('\n') to po_file_to_stdout in pocoface.
                      Sometimes this makes an extra NL display during output,
                      and sometimes it puts one there that's needed.  I dunno
                      why it's only sometimes needed, but the extra one when
                      it isn't needed doesn't hurt a thing, so...

    174   07/06/91  > Jim tries to hack in a String type.
                      It's to the point where it's generating OP_STRING_XXX
                      pretty much everywhere it should (except for freeing
                      local string variables), and also is running ok
                      treating a String like a (Popot) char * internally.
                    > Made it so void is no longer a valid type in numeric
                      expressions and void functions can't return a value.

    175   07/07/91  > Further work on string types.  Have a reference
                      counting protocol established that looks like
                      it'll do the job in postring.c.  Right now
                      it's possible for the reference count to
                      drop to 0 but the string not be freed during
                      a coercion from string to char * if the string
                      value originated in a function return.  I'm
                      keeping a list of all strings, so I can
                      walk the list freeing 0 ref. count things
                      periodically, but not sure where to do it
                      for minimal impact on performance.

    176   07/08/91  > Eliminated all (OP_IXXX + ido_type)
                      encodings in favor of xxx_ops[ido_type] (eg keeping
                      an array of multiplication ops, one for each
                      ido_type rather than adding the ido_type to OP_IMUL.)
                      This should be a good deal more flexible and less
                      error prone.
                    > Have string operations == != <= >= < > and +
                      (for concatenation) in place.  Basically
                      it all works now, but you're still going
                      to lose string memory sometimes.  String
                      arrays aren't freed up and string->(char *)
                      conversions leave an orphaned string reference.

    177   07/08/91  > Converted ugly #define's in pocoop.h to enums and
                      got rid of unused OP_CODE's.

    178   11/02/91  > New library function CelRelease() added.
                    > Fixed bug involving macros that have parens but no
                      parms.  A #define x() type macro would always give an
                      error when invoked as x(); now it works right.

    179   01/10/92  > Fixed bug in POCOFACE.C that was preventing the use
                      of multiple #pragma poco library statements in a
                      single poco program.

    180   05/10/92  > Fixed bug in get_member() in POCO.C that was generating
                      the wrong code to access an array inside a structure.
                      See comment block at head of get_member() for details.
                    > Added debugging routine po_dump_codebuf(), present only
                      when DEVELOPMENT is #define'd.  It dumps the current
                      contents of a code buffer to stdout during compile.
                    > Fixed array_decl() in DECLARE.C so that an array with
                      a size in a prototype is converted to a pointer type,
                      same as array[] without a size.
                    > Tweaked po_check_formatf() in POCOLIB.C so that a NULL
                      pointer passed for a %p format doesn't cause an abort,
                      since there's no problem with printing a NULL pointer.
                    > Added sanity check to po_get_libproto_line() in
                      POCOFACE.C to catch NULL proto string pointers.
                    > Changed the opcodes table in POCOOPS.C so that it now
                      now carries information on each instruction that
                      indicates whether the instruction disqualifies the
                      code sequence as a purely-constant sequence.  Right
                      now, this new info isn't being used for anything, but
                      it's in place.

    181  05/17/92   > Library changes, in poco and in root...
                    > New tween library         (\paa\pocotwen.c)
                    > New title library         (\paa\pocotitl.c)
                    > New flicplay library      (\paa\pocoflic.c)
                    > Updated string library    (strlib.c)
                        Added: strpbrk(), strtok(),  getenv()
                               strlwr(),  strupr(),  strerror()
                               strspn(),  strcspn(), atof()
                    > Updated memory library    (safefile.c)
                        Added: memchr()
                        Fixed: malloc() - No longer aborts on out-of-memory.
                    > Misc root library additions since v179:
                             SetBoxBevel(), GetBoxBevel()
                             CelRelease(),  GetPhysicalScreen(poe only)

    182  05/23/92   > Rewrote variable init routine.  It now lives in its
                      own module, VARINIT.C.  The rewrite fixes the bug that
                      prevented definition of an array of agregate types
                      with implied size.

    183  06/11/92   > Access to an array within a structure, where the
                      structure was accessed via a pointer, now works.
                      (Change in make_deref()).
                    > In po_code_pop(), we no longer attempt to optimize
                      away a do-nothing push/pop pair, since the routine
                      could be fooled by the data part of the preceeding op.
                    > #error now reports the rest of the text on the line
                      to the user, or "Error forced by #error" if there is
                      no other text on the line.
                    > Added #define EOF in resource\stdio.h.

    184  06/12/92   > You can now derference the return value of a function
                      that returns a pointer (ie, *strcpy(d,s) = 'x'; works).
                      Changes in FUNCCALL.C.
                    > Changed lotsa things in POCO.H to be enum types, for
                      better debugging.

    185  08/17/92   > Fixed reporting of errors that occur in preprocessor
                      statements.  It used to report a bogus line number.
                      Changes in POCO.C, POCO.H, PP.C, CHOPPER.C.
                    > An unexpected '}' is now reported.  Changes in POCO.C.
                    > Stack overflow is now checked on OP_ENTER so that
                      allocating more local vars than we have stack space
                      for is reported as an Err_poco_stack. Changes in RUNOPS.C.
                    > Line number reporting on stack traceback is now correct
                      when the error happens on or before the first line in
                      the function.  Changes in TRACE.C.
                    > Taking the address of an array by coding its name is
                      now properly recognized as a legal constant init
                      expression value. Changes in FOLD.C, POCO.C, VARINIT.C.
                    > The pure_const status of an expression following an
                      address-of (&) operator is now propogated upwards
                      properly. (eg, &(*ptr) is no longer seen as pure_const).
                      Changes in POCO.C.

    190  10/17/25   > Works on modern compilers, CPUs, and operating systems including
                      x64 and arm64
                    > Added a CMake build, running the test scripts through ctest
                    > Now uses libffi for bound funtion calls instead of assembly
                      trickery
                    > Performed the first steps towards separating Poco out into
                      its own library for use in other projects.
    191  09/17/26   > Poco is now a standalone, embeddable scripting VM with a
                      public <poco/poco.h> API (PocoVm, PocoProgram,
                      PocoActivation) and a generic native-module ABI; the old
                      Animator-internal entry points survive as legacy shims.
                    > The runtime is de-globalized and reentrant.  Activations
                      come from an acquire/reset/release pool, sources can be
                      compiled from memory, and .poe modules are found through
                      a search path.
                    > FFI hardening: by-value struct types, a cached cif with
                      preallocated call storage, correct float marshaling, and
                      bounded pointer/span contracts.  Bindings are split into
                      curated trusted and untrusted tiers.
                    > Bytecode is written to an architecture-portable,
                      BLAKE3-addressed container.  Binaries carry no source;
                      the loader validates what it is handed.
                    > Reworked CLI (long options, compile-to-binary,
                      detect-and-run) plus an opcode/line interactive debugger
                      that reads external source.
                    > Hosts can drive a program by name: global get/set,
                      main(argc, argv), and named calls with coerced arguments
                      and bounded span arguments.
                    > Several .poc sources can be linked together through a file
                      list or #pragma poco use, with a flat namespace, per-file
                      static privacy, and per-file debug identity.
                    > Animator Pro now links libpoco plus animhost instead of
                      embedding the interpreter directly.
                    > The MS-DOS build leftovers (poco.lnk, poco.obs,
                      pocfiles.inc, the src/test tree, versions.doc,
                      versnold.doc, and the Turbo C / Watcom makefiles) were
                      deleted.
                    > libpoco itself is unversioned; the public API is
                      POCO_API_VERSION 2.0.0.
