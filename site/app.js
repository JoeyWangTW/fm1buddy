// The fm1buddy installer page. The firmware is the latest release's, copied next to the page by the pages
// workflow (fw/): GitHub's release downloads can't be fetched from another site, its API can.

const REPO = 'konsumer/fm1buddy'
const FW = {
  feather: 'fw/fm1buddy-feather.uf2',
  cyd: 'fw/fm1buddy-cyd.bin',
  manifest: 'fw/manifest.json',
  version: 'fw/version.json'
}

const $ = (s) => document.querySelector(s)
const $$ = (s) => [...document.querySelectorAll(s)]

// ---------------------------------------------------------------------------------------------- tabs ---
function show(tab) {
  if (!document.getElementById(tab)) {
    tab = 'box'
  }
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

function noFirmware() {
  for (const el of $$('.fw-feather, .copy-uf2, .fw-cyd button, .fw-cyd-bin')) {
    el.setAttribute('aria-disabled', 'true')
    el.disabled = true
    el.removeAttribute('href')
  }
}

async function release() {
  const el = $('#release')
  let here = null
  try {
    here = (await json(FW.version)).tag
  } catch (_) {}
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
  $('.fw-cyd-bin').href = FW.cyd
  $('.fw-cyd').setAttribute('manifest', FW.manifest)

  const date = latest && latest.tag_name === here ? ` · ${new Date(latest.published_at).toLocaleDateString()}` : ''
  const url = `https://github.com/${REPO}/releases/tag/${encodeURIComponent(here)}`
  el.innerHTML = `Version <a href="${url}">${here}</a>${date}`
  if (latest && latest.tag_name !== here) {
    el.innerHTML += ` · a newer one, <a href="${latest.html_url}">${latest.tag_name}</a>, is on its way here`
  }
}

// ----------------------------------------------------------------------- the .uf2 onto the RPI-RP2 drive ---
async function copyUf2() {
  const status = $('.copy-status')
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
    say("That isn't the RPI-RP2 drive (it has an INFO_UF2.TXT). Hold BOOT while you plug the Feather in, and pick that drive.", true)
    return
  }
  say('Copying…')
  let wrote = false
  try {
    const data = await (await fetch(FW.feather)).arrayBuffer()
    const file = await dir.getFileHandle('fm1buddy-feather.uf2', { create: true })
    const w = await file.createWritable()
    await w.write(data)
    wrote = true
    await w.close()
    say('Done: the Feather restarts with fm1buddy.')
  } catch (e) {
    // the Feather restarts as soon as it has the whole file, and the drive goes away under us
    if (wrote) {
      say('Done: the Feather restarts with fm1buddy.')
    } else {
      say(`That didn't work (${e.message}): drag the file onto RPI-RP2 instead.`, true)
    }
  }
}

$('.copy-uf2').addEventListener('click', copyUf2)
release()
