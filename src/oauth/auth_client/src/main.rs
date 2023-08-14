#![allow(dead_code)]
#![allow(unused_variables)]
#![allow(unused_imports)]

use std::error::Error;
use reqwest::Url;
use reqwest::blocking;
use reqwest::header;
use std::collections::HashMap;
use base64::{encode,decode};
use serde::{Deserialize, Serialize};

static CLIENT_ID: &str = "r37zDwAtFauwo0stowcFEyL8";
static CLIENT_SECRET: &str = "lkxbjZLWA3saFdfe7EAp1mTxiJBVviRwj36AuSyremZM3kzc";
static SCOPE: &str = "profile";
static URL_ROOT: &str = "http://127.0.0.1:5000";
static AUTH_CODE: String = String::new();

fn get_auth_code() -> Result<(), Box<dyn Error>> {
	let params = [
		("response_type", "code"),
		("client_id", CLIENT_ID),
		("scope", SCOPE),
	];

//	let mut map = HashMap::new();	

	// construct URL
	let url_auth_str: &str = "/oauth/authorize";
	let url_auth: &str = &format!("{}{}", URL_ROOT, url_auth_str);
	let url = Url::parse_with_params(url_auth, &params)?;

	let body = blocking::get(url)?
		.headers()
//		.json()
		.send()
		.unwrap()
		;
		
	println!("body = {:?}", body);
	
	Ok(())
}

fn request_token() -> Result<(), Box<dyn Error>> {
	let params = [
		("grant_type", "authorization_code"),
		("scope", SCOPE),
		("code", AUTH_CODE),
	];
	
	// construct URL
	let url_auth_str: &str = "/oauth/token";
	let url_auth: &str = &format!("{}{}", URL_ROOT, url_auth_str);
	let url = Url::parse_with_params(url_auth, &params)?;

	let secret: &str = &format!("{}:{}", CLIENT_ID, CLIENT_SECRET);
	let encode_secret: String = encode(secret.as_string().as_bytes());

	//add base64 into headers
	let client = blocking::Client::new();
	let response = client::post(url)?
		.header(header::USER_AGENT, "authorization", encode_secret)
		.send()
		.unwrap();

	println!("body = {:?}", body);
	
	Ok(())
}

// Token request
fn main() {
	let _ = get_auth_code();
}
