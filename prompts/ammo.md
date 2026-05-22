# description
Introduce an ammo system for projectile weapons.

Projectile weapons can no longer fire for free by default. Each shot consumes ammo until the current amount reaches 0. At 0, that weapon cannot fire. The only exception is weapons configured to use infinite ammo, which can always fire.

Implementation requirements:

a) New editor config section: Ammo
- Add a new Ammo section in the editor, styled like the other configuration sections (panel list with image and name).
- Each ammo type must define:
	- name
	- HUD sprite
	- base maximum amount
- The base maximum amount is the normal cap for pickups. If the player is already at this amount, regular ammo pickups for that type do not increase ammo further.
- This is a base value and may be increased by other gameplay effects or pickups that explicitly raise the cap.

b) Weapon ammo settings
- In weapon properties, add an ammo type selection.
- The selection must contain:
	- all ammo types defined in the Ammo section
	- one special extra option: infinite
- For non-infinite ammo types, also configure ammo consumed per shot.
	- Different weapons may use the same ammo type but consume different amounts.
- Add a HUD display mode setting per weapon/ammo usage:
	- number mode: show sprite, then "x", then numeric amount
	- meter mode: show sprite, then a fixed-width fill bar based on current/maximum ammo
- For meter mode, allow selecting the meter color.
- For the infinite option, the HUD sprite is defined in global settings (because infinite is not a normal ammo type entry).

c) Item functions for ammo pickups
- Add item functions that grant ammo, with one selectable function per ammo type.
- Each ammo function has an amount parameter (same idea as coin/money amount).
- These ammo items must be usable anywhere normal items are used, including:
	- container contents
	- enemy drop tables

d) HUD behavior
- Show ammo only for ammo types that are relevant to weapons the player currently owns.
- Do not show unrelated ammo types.
- Do not show the infinite type as a tracked ammo amount.
- For each shown ammo type, render according to its configured display mode:
	- number mode: sprite + "x" + numeric amount
	- meter mode: sprite + fixed-width border + fill amount based on current/maximum ammo percentage
- In meter mode:
	- 0 ammo = empty bar
	- max ammo = full bar
	- fill color = configured meter color