(*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the "hack" directory of this source tree.
 *
 *)
(*
 * Copyright (c) Facebook, Inc. and its affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the "hack" directory of this source tree.
 *
 *)

open Refactor_sd_types
module LMap = Local_id.Map

(** Generates a fresh variable entity *)
val fresh_var : unit -> entity_

(** Initialise shape analysis environment *)
val init : Tast_env.env -> constraint_ list -> entity LMap.t -> env

(** Record a shape analysis constraint *)
val add_constraint : env -> constraint_ -> env

(** Ignore all existing constraints. The intention of this is to prevent
    unnecessary duplication of constraints when multiple environments need to
    be merged. *)
val reset_constraints : env -> env

(** Find an entity that a local variable points to *)
val get_local : env -> Local_id.t -> entity

(** Set an entity to a local variable *)
val set_local : env -> Local_id.t -> entity -> env

(** The first environment is the parent environment. The others are combined.
    This is useful in branching code. *)
val union : env -> env -> env -> env
