//// modules.php
<?hh
<<file:__EnableUnstableFeatures('modules')>>

new module C {}
//// no-module.php
<?hh
// Copyright (c) Facebook, Inc. and its affiliates. All Rights Reserved.
// TODO(T106200480)
<<file:__EnableUnstableFeatures('modules'), __Module>>

//// too-many-modules.php
<?hh
// Copyright (c) Facebook, Inc. and its affiliates. All Rights Reserved.
<<file:__EnableUnstableFeatures('modules'), __Module('A', 'B')>>
