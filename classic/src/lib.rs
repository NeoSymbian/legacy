// SPDX-License-Identifier: GPL-3.0-only
//! CLASSIC.EXE: Classic apps, the old S60 apps the legacy bridge (LEGACY.EXE, `host/`) registered with the shell, as
//! a neoui kit app. It reads them with `SETTINGS_INFO` kind `INFO_APPS` (protocol 1.14) and opens one with
//! `ACTION_OPEN_ENTRY`: the shell has the bridge start it, and the bridge brings it to the front (FRONT 1). When the
//! old app ends or goes back (FRONT 0) the shell returns here. The shell starts the bridge, hidden, with this app.
#![no_std]

extern crate alloc;

use alloc::rc::Rc;
use alloc::vec::Vec;
use core::cell::Cell;
use core::time::Duration;
use e32::dprintln;
use neoapp::{App, AppEvent, Page};
use neoui_proto::ipc as proto;
use neoui_proto::settings_app::info::OldApps;
use slint::{ComponentHandle, Model, ModelRc, VecModel};

slint::include_modules!();

e32::main!(main);

fn main() -> i32 {
    match run() {
        Ok(()) => 0,
        Err(e) => {
            dprintln!("classic: failed {}", e);
            e
        }
    }
}

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
    refresh(&app, &ui);
    // the bridge registers its apps a few seconds after it starts (AppArc's scan): read the list again while shown
    // shortcut: polled once a second; a watch on INFO_APPS when the shell offers one to apps other than Settings
    let shown = Rc::new(Cell::new(true));
    let timer = slint::Timer::default();
    let (a, w, s) = (app.clone(), ui.as_weak(), shown.clone());
    timer.start(
        slint::TimerMode::Repeated,
        Duration::from_secs(1),
        move || {
            if let (true, Some(ui)) = (s.get(), w.upgrade()) {
                refresh(&a, &ui);
            }
        },
    );
    app.draw()?;
    app.run(|e| {
        match e {
            AppEvent::Visible(v) => {
                shown.set(v);
                if v {
                    refresh(&app, &ui);
                }
            }
            AppEvent::Back => {
                neoapp::kit_back!(ui);
            }
            _ => {}
        }
        neoapp::kit_feedback!(ui, app);
        Ok(true)
    })
}

/// The bridge's apps into the grid, replaced only when they changed (a new model drops a tile's press).
fn refresh(app: &App, ui: &ClassicApp) {
    let c = ui.global::<Classic>();
    let list = app
        .settings_info(proto::INFO_APPS)
        .map(|b| OldApps::decode(&b));
    c.set_ok(!matches!(list, Err(_) | Ok(None)));
    let rows: Vec<OldApp> = list
        .ok()
        .flatten()
        .unwrap_or_default()
        .apps
        .iter()
        .map(|(id, name)| OldApp {
            id: *id as i32,
            name: name.as_str().into(),
        })
        .collect();
    let old = c.get_apps();
    if old.row_count() != rows.len()
        || rows
            .iter()
            .enumerate()
            .any(|(i, r)| old.row_data(i).as_ref() != Some(r))
    {
        dprintln!("classic: {} apps", rows.len());
        c.set_apps(ModelRc::new(VecModel::from(rows)));
    }
}
