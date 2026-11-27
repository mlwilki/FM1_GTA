// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
//
// FM-1 .fwsc packages in the browser: the package identity and the image the
// device reads during an update.

const BLOCKS = 20,
  BLK = 0x30,
  KEEP = 0x2f

// Official M-VAVE FM-1 firmware packages accepted by Rescue (unmodified FM-1.fwsc)
export const STOCK = [
  {
    version: 'V15',
    size: 699956,
    sha256: 'db1642b2b6fa5c2cccb11ffd13878068bb28601678d3644049f99dc40e7edb8a',
    product: 'FM-1_015'
  },
  {
    version: 'V16',
    size: 718196,
    sha256: 'd017fb65e70aca97ee29d944470c3515a74581df5c0576bf70de917c7b2b61d5',
    product: 'FM-1_016'
  }
]

export function matchStock(size, sha256Hex) {
  return STOCK.find((s) => s.size === size && s.sha256 === sha256Hex) || null
}

// the package identity ("FM-1_904"): one marker byte after each of the first 20 blocks
export function productOf(fwsc) {
  if (fwsc.length < BLOCKS * BLK) throw new Error('not an FM-1 package (too short)')
  let s = ''
  for (let i = 0; i < BLOCKS; i++) {
    const m = fwsc[i * BLK + KEEP]
    if (m !== 0x7d) s += String.fromCharCode((m - i - 1) & 0xff)
  }
  return s
}

// the image the device addresses during an update: the .fwsc without the 20 marker bytes
export function logicalImage(fwsc) {
  const out = new Uint8Array(fwsc.length - BLOCKS)
  for (let i = 0; i < BLOCKS; i++) out.set(fwsc.subarray(i * BLK, i * BLK + KEEP), i * KEEP)
  out.set(fwsc.subarray(BLOCKS * BLK), BLOCKS * KEEP)
  return out
}
