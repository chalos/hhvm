//// file1.php
<?hh
/**
 * Copyright (c) 2014, Facebook, Inc.
 * All rights reserved.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the "hack" directory of this source tree.
 *
 *
 */

newtype fbid = int;

//// file2.php
<?hh

function test(fbid $x): int {
  return $x;
}
