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
sizes.  If there are initialisations align them in their own column too
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
opening brace `{` for the function body.  Function names are where
possible prefixed by their module name so they are clearly identified as
to which module they belong.  An example of a static function:
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
``` 

Comments
--------

Use C-style comments, not C++/C99 style comments.  That means, don't use
`//` to comment lines, use `/*` and `*/` blocks instead.  You can use
Java-style comments for functions, but this is not a requirement.

