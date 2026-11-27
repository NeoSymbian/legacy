// SPDX-License-Identifier: GPL-3.0-only
//! CLASSIC.EXE: Classic apps, the old S60 apps the legacy bridge (LEGACY.EXE, `host/`) registered with the shell, as
//! a neoui kit app. It reads them with `SETTINGS_INFO` kind `INFO_APPS` (protocol 1.14), each one's launcher icon
//! with `ENTRY_THUMB` (the shell's cached 32 × 32 cut; the generic icon when there is none), and opens one with
//! `ACTION_OPEN_ENTRY`: the shell has the bridge start it, and the bridge brings it to the front (FRONT 1). When the
//! old app ends or goes back (FRONT 0) the shell returns here. The shell starts the bridge, hidden, with this app.
#![no_std]

extern crate alloc;

use alloc::rc::Rc;
use alloc::vec::Vec;
use core::cell::{Cell, RefCell};
use core::time::Duration;
use e32::dprintln;
use neoapp::{App, AppEvent, Page};
use neoui_proto::ipc as proto;
use neoui_proto::settings_app::info::OldApps;
use neoui_proto::settings_app::wire;
use slint::{ComponentHandle, Image, Model, ModelRc, Rgba8Pixel, SharedPixelBuffer, VecModel};

slint::include_modules!();

e32::main!(main);

/// Reads again while the list is empty after a show (the bridge registers its apps a few seconds after it starts,
/// once AppArc's scan is done): once a second, at most this many times, so a phone left on this screen stops asking.
const RETRIES: u32 = 15;

fn main() -> i32 {
    match run() {
        Ok(()) => 0,
        Err(e) => {
            dprintln!("classic: failed {}", e);
            e
        }
    }
}

/// Each app's icon by id, read once (`None`: the generic icon).
type Icons = Rc<RefCell<Vec<(u32, Option<Image>)>>>;

fn run() -> Result<(), i32> {
    let app = Rc::new(App::connect()?);
    let ui = ClassicApp::new().map_err(|_| -2)?;
    ui.global::<Theme>().set_dp(app.dp);
    let a = app.clone();
    ui.global::<Classic>().on_open(move |id| {
        // Slint's ints are signed: the UID's 32 bits come back as they went in
        let id = id as u32;
        let r = a.settings_action_with(proto::ACTION_OPEN_ENTRY, &id.to_le_bytes());
        dprintln!("classic: open {:08x} ({:?})", id, r);
        if r.is_err() {
            let _ = a.note("Couldn't open the app", "");
        }
    });
    ui.show().map_err(|_| -2)?;
    app.page(&Page {
        title: "Classic apps".into(),
        ..Page::default()
    })?;
    let icons: Icons = Rc::default();
    // the retry: running only while shown and the list is still empty, `left` tries at most
    let left = Rc::new(Cell::new(0u32));
    let timer = Rc::new(slint::Timer::default());
    let (a, w, i, l, t) = (
        app.clone(),
        ui.as_weak(),
        icons.clone(),
        left.clone(),
        timer.clone(),
    );
    timer.start(
        slint::TimerMode::Repeated,
        Duration::from_secs(1),
        move || {
            let Some(ui) = w.upgrade() else { return };
            l.set(l.get().saturating_sub(1));
            if refresh(&a, &ui, &i) > 0 || l.get() == 0 {
                t.stop();
            }
        },
    );
    timer.stop();
    let show = |app: &App, ui: &ClassicApp| {
        if refresh(app, ui, &icons) == 0 {
            left.set(RETRIES);
            timer.restart();
        }
    };
    show(&app, &ui);
    app.draw()?;
    app.run(|e| {
        match e {
            AppEvent::Visible(true) => show(&app, &ui),
            AppEvent::Visible(false) => timer.stop(),
            AppEvent::Back => {
                neoapp::kit_back!(ui);
            }
            _ => {}
        }
        neoapp::kit_feedback!(ui, app);
        Ok(true)
    })
}

/// The bridge's apps into the grid, replaced only when they changed (a new model drops a tile's press); how many
/// there are.
fn refresh(app: &App, ui: &ClassicApp, icons: &Icons) -> usize {
    let c = ui.global::<Classic>();
    let list = app
        .settings_info(proto::INFO_APPS)
        .map(|b| OldApps::decode(&b));
    c.set_ok(!matches!(list, Err(_) | Ok(None)));
    let list = list.ok().flatten().unwrap_or_default();
    let rows: Vec<OldApp> = list
        .apps
        .iter()
        .map(|(id, name)| {
            let icon = icon(app, icons, *id);
            OldApp {
                id: *id as i32,
                name: name.as_str().into(),
                has_icon: icon.is_some(),
                icon: icon.unwrap_or_default(),
            }
        })
        .collect();
    let more = usize::from(list.total).saturating_sub(rows.len());
    c.set_more(i32::try_from(more).unwrap_or(i32::MAX));
    let old = c.get_apps();
    let same = old.row_count() == rows.len()
        && rows.iter().enumerate().all(|(i, r)| {
            old.row_data(i)
                .is_some_and(|o| o.id == r.id && o.name == r.name)
        });
    if !same {
        dprintln!("classic: {} apps", rows.len());
        c.set_apps(ModelRc::new(VecModel::from(rows)));
    }
    list.apps.len()
}

/// App `id`'s launcher icon, read from the shell once (`ENTRY_THUMB`); `None` for the generic icon (none sent, or the
/// shell can't say).
fn icon(app: &App, icons: &Icons, id: u32) -> Option<Image> {
    if let Some((_, i)) = icons.borrow().iter().find(|(i, _)| *i == id) {
        return i.clone();
    }
    let image = app
        .entry_thumb(id, 0)
        .ok()
        .and_then(|b| wire::decode_thumb(&b).and_then(|(w, h, px)| argb_image(w, h, px)));
    icons.borrow_mut().push((id, image.clone()));
    image
}

/// Straight-alpha ARGB words (little endian BGRA bytes) as a Slint image; `None` for an empty one.
fn argb_image(w: u16, h: u16, px: &[u8]) -> Option<Image> {
    if w == 0 || h == 0 || px.len() != usize::from(w) * usize::from(h) * 4 {
        return None;
    }
    let mut buffer = SharedPixelBuffer::<Rgba8Pixel>::new(u32::from(w), u32::from(h));
    for (dst, src) in buffer
        .make_mut_bytes()
        .as_chunks_mut::<4>()
        .0
        .iter_mut()
        .zip(px.as_chunks::<4>().0)
    {
        dst.copy_from_slice(&[src[2], src[1], src[0], src[3]]);
    }
    Some(Image::from_rgba8(buffer))
}
