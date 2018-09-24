use hlt::log::Log;
use std::collections::HashMap;
use std::str::FromStr;

pub struct Constants {
    pub max_halite: i32,
    pub ship_cost: i32,
    pub dropoff_cost: i32,
    pub max_turns: i32,
    pub extract_ratio: i32,
    pub move_cost_ratio: i32,
    pub inspiration_enabled: bool,
    pub inspiration_radius: i32,
    pub inspiration_ship_count: i32,
    pub inspired_extract_ratio: i32,
    pub inspired_bonus_multiplier: f64,
    pub inspired_move_cost_ratio: i32,
}

impl Constants {
    pub fn new(log: &mut Log, string_from_engine: &str) -> Constants {
        let token_iter = string_from_engine.split(|c| " {},:\"\r\n".contains(c));
        let token_iter = token_iter.filter(|x| !x.is_empty());
        let tokens: Vec<&str> = token_iter.collect();

        if (tokens.len() % 2) != 0 {
            log.panic("Error: constants: expected even total number of key and value tokens from server.");
        }

        let mut map = HashMap::new();

        for i in (0..tokens.len()).step_by(2) {
            map.insert(tokens[i].to_string(), tokens[i+1].to_string());
        }

        Constants {
            ship_cost: Constants::get_value(log, &map, "NEW_ENTITY_ENERGY_COST"),
            dropoff_cost: Constants::get_value(log, &map, "DROPOFF_COST"),
            max_halite: Constants::get_value(log, &map, "MAX_ENERGY"),
            max_turns: Constants::get_value(log, &map, "MAX_TURNS"),
            extract_ratio: Constants::get_value(log, &map, "EXTRACT_RATIO"),
            move_cost_ratio: Constants::get_value(log, &map, "MOVE_COST_RATIO"),
            inspiration_enabled: Constants::get_value(log, &map, "INSPIRATION_ENABLED"),
            inspiration_radius: Constants::get_value(log, &map, "INSPIRATION_RADIUS"),
            inspiration_ship_count: Constants::get_value(log, &map, "INSPIRATION_SHIP_COUNT"),
            inspired_extract_ratio: Constants::get_value(log, &map, "INSPIRED_EXTRACT_RATIO"),
            inspired_bonus_multiplier: Constants::get_value(log, &map, "INSPIRED_BONUS_MULTIPLIER"),
            inspired_move_cost_ratio: Constants::get_value(log, &map, "INSPIRED_MOVE_COST_RATIO"),
        }
    }

    fn get_value<T: FromStr>(log: &mut Log, map: &HashMap<String, String>, key: &str) -> T {
        let s = Constants::get_string(log, map, key);
        match s.parse::<T>() {
            Ok(x) => x,
            Err(_) => log.panic(&format!("Error: constants: for {} got '{}' from server and failed to parse that.", key, s))
        }
    }

    fn get_string<'a>(log: &mut Log, map: &'a HashMap<String, String>, key: &str) -> &'a String {
        match map.get(key) {
            Some(x) => x,
            None => log.panic(&format!("Error: constants: server did not send {} constant.", key))
        }
    }
}
