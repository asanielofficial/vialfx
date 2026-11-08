/* VialFX: which kind of build this is.
 *
 * The CI build workflow turns VIALFX_NIGHTLY on (by editing the line below in its checkout) for every build that is
 * not a stable release, so test builds say NIGHTLY under the version number. Stable releases leave it at 0.
 *
 * This file is part of VialFX and is licensed under the GNU General Public License v3 or later.
 */

#pragma once

#define VIALFX_NIGHTLY 0
