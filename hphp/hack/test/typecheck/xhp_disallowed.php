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

class :div extends :blah {}

class Meh {
  protected function foo(): Xhp {
    return <div></div>;
  }
}

class :blah extends XHPTest {}
