use std::{
    collections::HashMap,
    sync::atomic::{AtomicBool, Ordering},
    time::{Duration, SystemTime},
};

use globed_shared::{
    anyhow::bail,
    base64::{engine::general_purpose as b64e, Engine as _},
    *,
};

const FLUSH_PERIOD: Duration = Duration::from_secs(3);
const DELETER_PERIOD: Duration = Duration::from_mins(10);

#[derive(Clone)]
struct AccountEntry {
    pub account_id: i32,
    pub name: String,
    pub authcode: u32,
    pub message_id: i32,
}

pub struct AccountVerifier {
    http_client: reqwest::Client,
    account_id: i32,
    account_gjp: String,
    message_cache: SyncMutex<Vec<AccountEntry>>,
    last_update: SyncMutex<SystemTime>,
    outdated_messages: SyncMutex<IntSet<i32>>,
    is_enabled: AtomicBool,
}

impl AccountVerifier {
    pub fn new(account_id: i32, account_gjp: String, enabled: bool) -> Self {
        let http_client = reqwest::ClientBuilder::new().user_agent("").build().unwrap();
        Self {
            http_client,
            account_id,
            account_gjp,
            message_cache: SyncMutex::new(Vec::new()),
            last_update: SyncMutex::new(SystemTime::now()),
            outdated_messages: SyncMutex::new(IntSet::default()),
            is_enabled: AtomicBool::new(enabled),
        }
    }

    pub fn set_enabled(&self, state: bool) {
        self.is_enabled.store(state, Ordering::Relaxed);
    }

    pub async fn verify_account(&self, account_id: i32, account_name: &str, authcode: u32) -> Option<i32> {
        if !self.is_enabled.load(Ordering::Relaxed) {
            return Some(0);
        }

        let request_time = SystemTime::now();
        loop {
            let last_refresh = *self.last_update.lock();

            if last_refresh > request_time {
                break;
            }

            tokio::time::sleep(FLUSH_PERIOD / 2).await;
        }

        let cache = self.message_cache.lock();
        for msg in &*cache {
            if msg.account_id == account_id && msg.name.eq_ignore_ascii_case(account_name) && msg.authcode == authcode {
                return Some(msg.message_id);
            }
        }

        None
    }

    pub async fn run_refresher(&self) {
        let mut interval = tokio::time::interval(FLUSH_PERIOD);
        interval.set_missed_tick_behavior(tokio::time::MissedTickBehavior::Skip);
        interval.tick().await;

        loop {
            interval.tick().await;

            if !self.is_enabled.load(Ordering::Relaxed) {
                continue;
            }

            match self.flush_cache().await {
                Ok(()) => {}
                Err(err) => {
                    warn!("failed to refresh account verification cache: {err}");
                }
            }
        }
    }

    // run the task that periodically deletes the stale messages
    pub async fn run_deleter(&self) {
        let mut interval = tokio::time::interval(DELETER_PERIOD);
        interval.tick().await;

        loop {
            interval.tick().await;

            if !self.is_enabled.load(Ordering::Relaxed) {
                continue;
            }

            match self.delete_outdated().await {
                Ok(()) => {}
                Err(err) => {
                    warn!("failed to delete outdated messages: {err}");
                }
            }
        }
    }

    async fn delete_outdated(&self) -> anyhow::Result<()> {
        let outdated = {
            let mut mtx = self.outdated_messages.lock();
            if mtx.is_empty() {
                return Ok(());
            }

            // unnecessary clone but oh well
            let cloned = mtx.clone();
            mtx.clear();
            drop(mtx);

            cloned
        };

        let outdated_str = outdated.into_iter().map(|x| x.to_string()).collect::<Vec<String>>().join(",");

        let result = self
            .http_client
            .post("https://www.boomlings.com/database/deleteGJMessages20.php")
            .form(&[
                ("accountID", self.account_id.to_string()),
                ("gjp2", self.account_gjp.clone()),
                ("secret", "Wmfd2893gb7".to_owned()),
                ("messages", outdated_str),
            ])
            .send()
            .await;

        let response = match result {
            Err(err) => {
                warn!("Failed to make a request to boomlings: {}", err.to_string());
                bail!("boomlings error: {err}");
            }
            Ok(x) => x,
        }
        .text()
        .await?;

        if response == "-1" {
            bail!("boomlings returned -1!");
        }

        Ok(())
    }

    async fn flush_cache(&self) -> anyhow::Result<()> {
        let result = self
            .http_client
            .post("https://www.boomlings.com/database/getGJMessages20.php")
            .form(&[
                ("accountID", self.account_id.to_string()),
                ("gjp2", self.account_gjp.clone()),
                ("secret", "Wmfd2893gb7".to_string()),
            ])
            .send()
            .await;

        let mut response = match result {
            Err(err) => {
                warn!("Failed to make a request to boomlings: {}", err.to_string());
                bail!("boomlings error: {err}");
            }
            Ok(x) => x,
        }
        .text()
        .await?;

        if response == "-1" {
            bail!("boomlings returned -1!");
        }

        let mut msg_cache = self.message_cache.lock();
        msg_cache.clear();

        *self.last_update.lock() = SystemTime::now();

        if response == "-2" {
            // -2 means no messages
            return Ok(());
        }

        let octothorpe = response.find('#');

        if let Some(octothorpe) = octothorpe {
            response = response.split_at(octothorpe).0.to_string();
        }

        let message_strings = response.split('|');
        for string in message_strings {
            // example responses:
            // 6:Lxwnar:3:153455117:2:25517837:1:78431572:4:bWVzc2FnZQ==:8:0:9:0:7:10 hours
            // 6:xponder:3:224655218:2:25222473:1:78431560:4:aGkgZ3V5eg==:8:0:9:0:7:10 hours
            let values = parse_robtop_string(string, ':');

            let message_id = values.get("1");
            let title = values.get("4");
            let author_name = values.get("6");
            let author_id = values.get("2");
            let age = values.get("7");

            if message_id.is_none() || title.is_none() || author_name.is_none() || author_id.is_none() || age.is_none() {
                warn!("ignoring invalid message: one of the attrs is none");
                continue;
            }

            let title = b64e::URL_SAFE.decode(title.unwrap())?;
            let title = String::from_utf8(title)?;

            let message_id = message_id.unwrap().parse::<i32>()?;
            let author_name = author_name.unwrap();
            let author_id = author_id.unwrap().parse::<i32>()?;
            let age = age.unwrap();

            // if the message is old, queue it for deletion
            if age.contains("hours") || age.contains("days") || age.contains("months") || age.contains("years") {
                self.outdated_messages.lock().insert(message_id);
                continue;
            }

            let title = title.split_once("##c## ");
            let authcode = if let Some((_void, authcode)) = title {
                authcode.parse::<u32>()?
            } else {
                continue;
            };

            // info!("adding message to cache: {author_id}, {author_name}, {authcode}");

            msg_cache.push(AccountEntry {
                account_id: author_id,
                name: (*author_name).to_string(),
                authcode,
                message_id,
            });
        }

        Ok(())
    }
}

fn parse_robtop_string(data: &str, separator: char) -> HashMap<&str, &str> {
    let pairs: Vec<&str> = data.split(separator).collect();
    let mut map = HashMap::new();

    for i in (0..pairs.len()).step_by(2) {
        if let Some(key) = pairs.get(i) {
            if let Some(value) = pairs.get(i + 1) {
                map.insert(*key, *value);
            }
        }
    }

    map
}
