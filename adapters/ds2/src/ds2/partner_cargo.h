#pragma once
// DS2-internal: the partner's carried cargo where the game counts or moves cargo (see ds2/partner_cargo.cpp), and a guest's
// moves into and out of shared lockers. Installed early; `diagnostics` logs every slot call that touches a locker.
namespace partner_cargo {

void installEarly(bool diagnostics);

}  // namespace partner_cargo
