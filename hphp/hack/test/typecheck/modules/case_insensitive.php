//// modules.php
<?hh
<<file:__EnableUnstableFeatures('modules')>>

new module a {}
new module A {}
new module B {}
//// a.php
<?hh
// Copyright (c) Facebook, Inc. and its affiliates. All Rights Reserved.
<<file:__EnableUnstableFeatures('modules'), __Module('a')>>

internal function f(): void {}


//// A.php
<?hh
<<file:__EnableUnstableFeatures('modules'), __Module('A')>>

function g(): void {
  f(); // ERROR: we are in `A`, not `a`
}

//// b.php
<?hh
<<file:__EnableUnstableFeatures('modules'), __Module('b')>> // ERROR: no such new module `b`

// TODO(T108206307) You _need_ a top level symbol to attach a new module to in order
// to get an unbound new module name error.
function h(): void {}
