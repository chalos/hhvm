<?hh // strict

class C {}

<<__Rx, __MutableReturn>>
function make(): C {
  $a = new C();
  \HH\Rx\freeze($a);
  // not OK - returns immutable
  return $a;
}
