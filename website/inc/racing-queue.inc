<?php
require_once('inc/scenes.inc');
require_once('inc/roster.inc');
require_once('inc/schedule_one_round.inc');

function queue_entry_for($roundid) {
  return read_single_row('SELECT * FROM RacingQueue'
                         .' INNER JOIN Rounds USING (classid, round)'
                         .' WHERE roundid = :roundid',
                         array(':roundid' => $roundid), PDO::FETCH_ASSOC);
}

// Finds the next queue entry after the given one.
function queue_entry_after($seq) {
  return read_single_row('SELECT * FROM RacingQueue'
                         .' WHERE seq = (SELECT MIN(seq) FROM RacingQueue'
                         .'              WHERE seq > :seq)',
                         array(':seq' => $seq), PDO::FETCH_ASSOC);
}

function round_is_completed($roundid) {
  return read_single_value('SELECT COUNT(*) FROM RaceChart'
                           .' WHERE roundid = :roundid'
                           .'  AND (completed IS NULL OR completed = \'\')',
                           array(':roundid' => $roundid)) == 0;
}

// Returns false, and emits <failure>, if unable to activate the queue entry
function activate_queue_entry(&$queue_entry) {
  global $db;
  $roundid = read_single_value('SELECT roundid FROM Rounds'
                               .' WHERE classid = :classid AND round = :round',
                               array(':classid' => $queue_entry['classid'],
                                     ':round' => $queue_entry['round']));
  if (!$roundid) {
    $roundid = make_new_round($queue_entry['classid'], $queue_entry['round']);
  }
  if (read_single_value('SELECT COUNT(*) FROM Roster WHERE roundid = :roundid',
                                     array(':roundid' => $roundid)) == 0) {
    if ($queue_entry['round'] == 1) {
      $is_aggregate = true;
      $constituent_classids = read_single_value('SELECT constituents FROM Classes'
                                                .' WHERE classid = :classid',
                                                array(':classid' => $queue_entry['classid']));
      $stmt = $db->prepare('SELECT roundid FROM Rounds r1'
                           .' WHERE r1.classid IN ('.$constituent_classids.')'
                           .' AND round = (SELECT MAX(round) FROM Rounds r2'
                           .'              WHERE r2.classid = r1.classid)');
      $stmt->execute();
      $constituent_roundids = implode(',', $stmt->fetchAll(PDO::FETCH_COLUMN));
    } else {
      // Follow-on round
      $is_aggregate = false;
      $constituent_roundids = read_single_value('SELECT roundid FROM Rounds'
                                                .' WHERE classid = :classid'
                                                .'  AND round = :round',
                                                array(':classid' => $queue_entry['classid'],
                                                      ':round' => $queue_entry['round'] - 1));
    }
    populate_roster($roundid, $queue_entry['classid'], $constituent_roundids,
                    $queue_entry['bucketed'], $queue_entry['bucket_limit'], $is_aggregate);
  }
  if ($queue_entry['n_times_per_lane'] > 0 &&
      read_single_value('SELECT COUNT(*) FROM RaceChart'
                        .' WHERE roundid = :roundid',
                        array(':roundid' => $roundid)) == 0) {
    $nlanes = get_lane_count();
    if ($nlanes > 0) {
      schedule_one_round($roundid, $nlanes, $queue_entry['n_times_per_lane'], /* trace */ false);
    }
  }

  $current = get_running_round();
  $current['roundid'] = $roundid;
  return advance_heat($current, 1, /* report_failure */ true);
}

/* When racing ends, at the completion of a round (all heats have been run), if
  the current head of RacingQueue matches the just-ending roundid, then:
  - Activate the next queue entry (generate the schedule if need be, set the current round).
  - If sceneid_at_finish, then set that, otherwise
  - if continue_racing_at_end, then set racing on (it was just turned off), otherwise
  - do nothing (leave racing off)
*/
function perform_queue_after_round($roundid) {
  $entry = queue_entry_for($roundid);
  $after = queue_entry_after($entry['seq']);
  if ($after) {
    activate_queue_entry($after);
  }
  if ($entry['sceneid_at_finish'] > 0) {
    // TODO We want to queue a new scene a short time after racing ends, not
    // apply the scene immediately.
    apply_scene($entry['sceneid_at_finish']);
  } else if ($entry['continue_racing']) {
    // Since we were racing before, DON'T re-apply the racing scene, as user may
    // have customized their set-up for whatever reason.
    set_racing_state(1);
  }
}
?>