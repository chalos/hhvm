<?hh // strict
<<file: __EnableUnstableFeatures('coeffects_provisional')>>

class C {
  public function __construct(public int $v) {}
}

<<__RxShallow>>
function f(C $c)[rx_shallow]: void {
  // not OK - RxShallow functions behave like reactive
  $c->v = 5;
}
