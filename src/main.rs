use fltk::{app, prelude::*, window::Window};

fn main() {
    let a = app::App::default();
    let mut wind = Window::default()
        .with_label("Nashville")
        .with_size(400, 400)
        .center_screen();
    wind.end();
    wind.show();
    a.run().unwrap();
}
