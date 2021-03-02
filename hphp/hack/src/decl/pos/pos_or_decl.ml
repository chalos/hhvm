(*
 * Copyright (c) Facebook, Inc. and its affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the "hack" directory of this source tree.
 *
 *)

type t = Pos.t [@@deriving eq, ord, show]

let none : t = Pos.none

let btw = Pos.btw

let make_decl_pos : Pos.t -> Decl_reference.t -> t =
 (fun p _decl -> (* TODO *) p)