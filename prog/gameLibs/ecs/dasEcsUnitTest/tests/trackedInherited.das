require ecs
require DasEcsUnitTest

// trackedInheritedChild declares _tracked for a component its parent declares

[es(track=inherited_track_val, REQUIRE=inherited_track_val)]
def das_track_inherited_changed(evt : Event)
  set_test_value("TrackInheritedChanged", 1)

[es(on_event=EventStart)]
def das_modify_inherited_track_val(evt : Event; var inherited_track_val : int&)
  inherited_track_val += 1

[es(on_event=EventEnd)]
def das_verify_inherited_track_val(evt : Event)
  verify(get_test_value("TrackInheritedChanged") == 1)
