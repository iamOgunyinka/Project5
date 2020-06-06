#!/usr/bin/env python
import os
from flask import Flask
from flask_migrate import Migrate
from flask_sqlalchemy import SQLAlchemy

migrate = Migrate()
db = SQLAlchemy()

class User(db.Model):
	__tablename__ = 'tb_users'
	id = db.Column(db.Integer, primary_key=True, unique=True, index=True)
	role = db.Column(db.Integer, nullable=False)
	username = db.Column(db.String(64), nullable=False, unique=True)
	password = db.Column(db.String(128), nullable=False, unique=False)
	tasks = db.relationship('Task', backref='user')
	uploads = db.relationship('Upload', backref='user')

class Website(db.Model):
	__tablename__ = 'tb_websites'
	id= db.Column(db.Integer, primary_key=True, unique=True, index=True)
	nickname = db.Column(db.String(64), nullable=False, unique=True)
	address = db.Column(db.String(256), nullable=False, unique=True)

class Task(db.Model):
	__tablename__ = 'tb_tasks'
	id = db.Column(db.Integer, primary_key=True, unique=True, index=True)
	status = db.Column(db.Integer, nullable=False, unique=False, default=0)
	total_numbers = db.Column(db.Integer, nullable=False, unique=False)
	scheduler_id = db.Column(db.Integer, db.ForeignKey('tb_users.id'), nullable=False)
	date_scheduled = db.Column(db.String(32), nullable=False, unique=True)
	website_id = db.Column(db.Integer, nullable=False)
	uploads = db.Column(db.Text, nullable=False)
	processed = db.Column(db.Integer, nullable=False, default=0, unique=False)
	ok_count = db.Column(db.Integer, nullable=False, default=0, unique=False)
	not_ok_count = db.Column(db.Integer, nullable=False, default=0, unique=False)
	unknown_count = db.Column(db.Integer, nullable=False, default=0, unique=False)
	per_ip = db.Column(db.Integer, nullable=False, default=0, unique=False)
	ip_used = db.Column(db.Integer, nullable=False, default=0, unique=False)
	input_filename = db.Column(db.String(128), nullable=True)
	ok_file = db.Column(db.String(128), nullable=True)
	ok2_file = db.Column(db.String(128), nullable=True)
	not_ok_file = db.Column(db.String(128), nullable=True)
	unknown_file = db.Column(db.String(128), nullable=True)

class Upload(db.Model):
	__tablename__ = 'tb_uploads'
	id = db.Column(db.Integer, primary_key=True, unique=True, index=True)
	uploader_id = db.Column(db.Integer, db.ForeignKey('tb_users.id'), nullable=False)
	filename = db.Column(db.String(128), nullable=False)
	name_on_disk = db.Column(db.String(128), nullable=False)
	upload_date = db.Column(db.String(32), nullable=False)
	total_numbers = db.Column(db.Integer, nullable=False)
	status = db.Column(db.Integer, nullable=False, default=0)
	numbers = db.relationship('Number', backref='upload')

def create_app(config_name):
	app = Flask(__name__)
	# apply configuration
	cfg = os.path.join(os.getcwd(), 'config', config_name + '.py')
	app.config.from_pyfile(cfg)
	
	# initialize extensions
	db.init_app(app)
	migrate.init_app(app, db)
	return app

app = create_app('development')


if __name__ == '__main__':
	with app.app_context():
		db.drop_all()
		db.create_all()
		print('Created')
