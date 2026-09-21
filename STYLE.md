Code Style
==========

Since the beginning of 2026, any modules modified were completely
re-indented for a code style as outlined in this document
For new and any modified code please use the below code style, but do
not mix tabs and spaces, if the existing code has not yet been
completely reflowed yet.

Line width
----------

Stick to 80 characters of visible code, preferably break before 76
characters.  Overflows are allowed for unbreakable things like URLs.

Indentation
-----------

- use [Allman style](https://en.wikipedia.org/wiki/Indentation_style#Allman_style)
  (this means the `{` is on the next line, at the indentation level of the
  statement it is part of)
- indentation levels are done via spaces (not tabs)
- indentation level is 2 visual spaces
- indentation for `(` is lined up to the level of the `(`
- For Vim users, this is set via
  `vim: set ts=2 sw=2 expandtab cino+=\:0:`

Spacing after if, while, do, for, switch, etc.
----------------------------------------------

The `(cond)` block is separated by a space after the `if`, `while`,
`do` and the like.  Similarly a space is between `for` and the `(`
following it.

Spacing for comparisons and operations, etc.
--------------------------------------------

Surround operators such as `<`, `+` with spaces.  Use a space after a
`,` but not before.  Exceptions are the shorthands like `++`.  Examples:
```
  len++;

  foo = bar + len - 1;

  fnord(x, y, 2);

  x = myarr[i + 1];
```

Conditions
----------

For any multi-conditions, put each condition on its own line.  This not
only allows a debugger to point to the expression, but makes it visual
that there are multiple conditions.  For example:
```
  if (foo &&
      (bar > 0 ||
       baz == 0))
```
This also applies to for-loops, so if the 3 statements don't fit break
them up per condition:
```
  for (var = fnord->state->iterable;
       (var != NULL &&
        var->element->type != SOMETHING_WEIRD);
       var = var->next)
```

Declarations
------------

Declarations go on top of the scope they are visible in.  Definitions
are aligned such that the type and the variable name are in two columns.
Pointer asterisks (`*`) are in front of the variable name's alignment.
Try to allow the compiler to make proper alignment, e.g. not having to
waste space by alignment rules by going from large to small storage
sizes.  Do not declare multiple variables on the same line, use separate
declarations for extra clarity and avoid mistakes with pointer types.
If there are initialisations align them in their own column too
on the `=`. Example:
```
  char      buf[2048];
  mything **whatever = NULL;
  atom_ctx *atom;
  array_t  *arr;
  size_t    len;
  int       i;
  bool      istrue   = false;
```

Function declarations
---------------------

Function declarations are multi-line declarations where emphasis is made
to the arguments of the function.  A declaration starts with any
modifiers like `static` and its return type on their own line, followed
by the name of the function on its own line, and then the argument
variables formatted like the Declarations section above between `(` and
`)` on their own lines, indented by one level (2 spaces) followed by the
opening brace `{` for the function body.  The only exception to this is
a parameterless function, for which the mandatory `(void)` can be placed
directly after the function name to distinguish it clearly from any
arguments, should there be any.  Function names are where possible
prefixed by their module name so they are clearly identified as to which
module they belong.  An example of a static function:
```
static char *mylib_funcname
(
  context *ctx,
  int      id,
  char    *name
)
{
  /* something */
}

mylib_t *mylib_new(void)
{
  /* something */
}
``` 

Scope braces
------------

Use balanced use of braces in if-else conditions.  If a single
expression is used, braces may be omitted.  This is not a hard rule,
however, in some scenarios such as multi-line conditions it reads easier
to use braces even though a single expression is used.  For do-while
loops, put the while on a new line after the scope closing `}`.
Examples:
```
  if (foo)
    dobar = false;

  if (foo)
    fnord *= 2;
  else
    fnord  = 0;

  if (bar == 0 &&
      fnord == 1)
  {
    foo = true;
  }

  if (baz > 0)
  {
    do_foo(ctx);
    visited = true;
  }
  else
  {
    warn("baz '%u' > 0", baz);
  }

  do
  {
    /* blah */
  }
  while (mycondition);
```

Comments
--------

Use C-style comments, not C++/C99 style comments.  That means, don't use
`//` to comment lines, use `/*` and `*/` blocks instead.  You can use
Java-style comments for functions, but this is not a requirement.

Assignments
-----------

Blocks of assignments should be aligned on the `=` character to read
more tabular.  Shorthand characters such as `+`, `-`, `*`, etc go before
the `=` alignment.  Example:
```
  isset   = true;
  nmemb  += this_thing;
  ret    += this_thing;
  ptr    -= this_thing;
  fnordzy = myfunc(this_thing);

  ret          = xzalloc(sizeof(*ret));
  ret->memb1   = foo;
  ret->memb2   = true;
  ret->mypoint = &this_thing;
```

strcmp, strncmp and memcmp
--------------------------

Use explicit comparisons against the return value of these functions.
In particular avoid constructs like `!strcmp(...)` as that reads
confusing like a negative statement, while it actually evaluates to
`true` for the matching condition; just use `strcmp(...) == 0` instead.

memset
------

Use the `VAL_CLEAR` and `VALP_CLEAR` macros to clear structs, arrays and
pointers.  Simple examples are:
```
  struct stat st;

  VAL_CLEAR(st);

...

  struct {
    char buf[16];
    bool set;
  } *myelem;

  myelem = xmalloc(sizeof(*myelem));
  VALP_CLEAR(myelem->buf);
```
