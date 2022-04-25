//// modules.php
<?hh
<<file:__EnableUnstableFeatures('modules')>>

new module A {}
new module B {}

//// A.php
<?hh
// Copyright (c) Facebook, Inc. and its affiliates. All Rights Reserved.

<<file:__EnableUnstableFeatures('modules'), __Module('A')>>

class A {
  internal static int $x = 0;
}


function a(): void {
  A::$x = 1;
}


//// B.php
<?hh
// Copyright (c) Facebook, Inc. and its affiliates. All Rights Reserved.

<<file:__EnableUnstableFeatures('modules'), __Module('B')>>

<<__Module("B")>>
function b(): void {
  A::$x = 1;
}

//// no-module.php
<?hh
// Copyright (c) Facebook, Inc. and its affiliates. All Rights Reserved.

function none(): void {
  A::$x = 1;
}
