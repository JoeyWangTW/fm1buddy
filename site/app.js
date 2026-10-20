// The fm1buddy installer page. The firmware is the latest release's, copied next to the page by the pages
// workflow (fw/): GitHub's release downloads can't be fetched from another site, its API can.

const REPO = 'konsumer/fm1buddy'
const FW = {
  feather: 'fw/fm1buddy-feather.uf2',
  pico: 'fw/fm1buddy-pico.uf2',
  cyd: 'fw/fm1buddy-cyd.bin',
  s3: 'fw/fm1buddy-cyd-s3.bin',
  manifest: 'fw/manifest.json',
  manifestS3: 'fw/manifest-s3.json',
  version: 'fw/version.json'
}

const $ = (s) => document.querySelector(s)
const $$ = (s) => [...document.querySelectorAll(s)]

// ---------------------------------------------------------------------------------------------- tabs ---
function show(tab) {
  const el = tab && document.getElementById(tab)
  const panel = el && el.closest('.panel') // a heading in a tab: that tab
  tab = panel ? panel.id : 's3'
  for (const b of $$('.tabs button')) {
    b.setAttribute('aria-selected', b.dataset.tab === tab)
  }
  for (const p of $$('.panel')) {
    p.hidden = p.id !== tab
  }
}

for (const b of $$('.tabs button')) {
  b.addEventListener('click', () => {
    history.replaceState(null, '', '#' + b.dataset.tab)
    show(b.dataset.tab)
  })
}
for (const a of $$('[data-go]')) {
  a.addEventListener('click', (e) => {
    e.preventDefault()
    history.replaceState(null, '', '#' + a.dataset.go)
    show(a.dataset.go)
    scrollTo({ top: 0, behavior: 'smooth' })
  })
}
show(location.hash.slice(1))

// ------------------------------------------------------------------------------------------- release ---
async function json(url) {
  const r = await fetch(url, { cache: 'no-cache' })
  if (!r.ok) {
    throw new Error(r.status)
  }
  return r.json()
}

function noFirmware(sel = '.fw-feather, .fw-pico, .copy-uf2, .fw-cyd button, .fw-cyd-bin, .fw-s3 button, .fw-s3-bin') {
  for (const el of $$(sel)) {
    el.setAttribute('aria-disabled', 'true')
    el.disabled = true
    el.removeAttribute('href')
  }
}

async function release() {
  const el = $('#release')
  let here = null
  let pico = false
  let s3 = false
  try {
    const v = await json(FW.version)
    here = v.tag
    pico = !!v.pico
    s3 = !!v.s3
    cases(v.cases || [])
  } catch (_) {
    cases([])
  }
  let latest = null
  try {
    latest = await json(`https://api.github.com/repos/${REPO}/releases/latest`)
  } catch (_) {}

  if (!here) {
    noFirmware()
    el.innerHTML = latest ? `The latest release, <a href="${latest.html_url}">${latest.tag_name}</a>, isn't on this page yet: get the files from there.` : `There's no release yet: <a href="https://github.com/${REPO}">build it from the source</a>.`
    return
  }
  $('.fw-feather').href = FW.feather
  if (pico) {
    $('.fw-pico').href = FW.pico
  } else {
    noFirmware('.fw-pico, .copy-uf2[data-fw="pico"]') // releases from before it
  }
  $('.fw-cyd-bin').href = FW.cyd
  $('.fw-cyd').setAttribute('manifest', FW.manifest)
  if (s3) {
    $('.fw-s3-bin').href = FW.s3
    $('.fw-s3').setAttribute('manifest', FW.manifestS3)
  } else {
    noFirmware('.fw-s3 button, .fw-s3-bin') // releases from before it
  }

  const date = latest && latest.tag_name === here ? ` · ${new Date(latest.published_at).toLocaleDateString()}` : ''
  const url = `https://github.com/${REPO}/releases/tag/${encodeURIComponent(here)}`
  el.innerHTML = `Version <a href="${url}">${here}</a>${date}`
  if (latest && latest.tag_name !== here) {
    el.innerHTML += ` · a newer one, <a href="${latest.html_url}">${latest.tag_name}</a>, is on its way here`
  }
}

// ------------------------------------------------------------------------------------------- cases ---
const SCAD = `https://github.com/${REPO}/blob/main/case/fm1buddy-case.scad`

function cases(files) {
  for (const box of $$('.case-links')) {
    const config = box.dataset.case || box.closest('.case').dataset.case
    const has = (name) => files.includes(`fm1buddy-case-${name}.stl`)
    const link = (name, label) => `<a class="button${label.startsWith('With') ? ' secondary' : ''}" href="fw/fm1buddy-case-${name}.stl" download>${label}</a>`
    if (!has(`${config}-base`)) {
      box.innerHTML = `<span class="note">The STLs come with the next release; or make them from <a href="${SCAD}">the OpenSCAD file</a>.</span>`
      continue
    }
    box.innerHTML = [link(`${config}-base`, 'Base'), link(`${config}-lid`, 'Lid'), has(`${config}-battery-base`) ? link(`${config}-battery-base`, 'With a battery: base') : '', has(`${config}-battery-lid`) ? link(`${config}-battery-lid`, 'With a battery: lid') : ''].join(' ')
  }
}

// ----------------------------------------------------------------------- the .uf2 onto the RPI-RP2 drive ---
async function copyUf2(button) {
  const fw = button.dataset.fw
  const board = fw === 'pico' ? 'board' : 'Feather'
  const status = button.closest('li').querySelector('.copy-status')
  const say = (msg, bad = false) => {
    status.hidden = false
    status.textContent = msg
    status.classList.toggle('bad', bad)
  }
  if (!window.showDirectoryPicker) {
    say("This browser can't write to a drive: download the file and drag it onto RPI-RP2.", true)
    return
  }
  let dir
  try {
    dir = await showDirectoryPicker({ id: 'rpi-rp2', mode: 'readwrite' })
  } catch (_) {
    return // cancelled
  }
  try {
    await dir.getFileHandle('INFO_UF2.TXT')
  } catch (_) {
    say(`That isn't the RPI-RP2 drive (it has an INFO_UF2.TXT). Hold BOOT while you plug the ${board} in, and pick that drive.`, true)
    return
  }
  say('Copying…')
  let wrote = false
  try {
    const data = await (await fetch(FW[fw])).arrayBuffer()
    const file = await dir.getFileHandle(`fm1buddy-${fw}.uf2`, { create: true })
    const w = await file.createWritable()
    await w.write(data)
    wrote = true
    await w.close()
    say(`Done: the ${board} restarts with fm1buddy.`)
  } catch (e) {
    // the Feather restarts as soon as it has the whole file, and the drive goes away under us
    if (wrote) {
      say(`Done: the ${board} restarts with fm1buddy.`)
    } else {
      say(`That didn't work (${e.message}): drag the file onto RPI-RP2 instead.`, true)
    }
  }
}

for (const b of $$('.copy-uf2')) {
  b.addEventListener('click', () => copyUf2(b))
}
release()
