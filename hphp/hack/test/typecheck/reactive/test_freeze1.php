<?hh // strict
<<file: __EnableUnstableFeatures('coeffects_provisional')>>
class C {
  <<__Rx>>
  public function __construct(public int $val)[rx] {}
}

<<__Rx>>
function basic()[rx]: void {
  $z = \HH\Rx\mutable(new C(7)); // $z is mutable
  $z1 = \HH\Rx\freeze($z); // $z1 is immutable
  // error
  $z1->val = 5;
}
