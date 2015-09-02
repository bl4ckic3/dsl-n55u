/*
   Unix SMB/CIFS implementation.
   Core SMB2 server

   Copyright (C) Stefan Metzmacher 2009

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 3 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "includes.h"
#include "smbd/globals.h"
#include "../libcli/smb/smb_common.h"

static struct tevent_req *smbd_smb2_write_send(TALLOC_CTX *mem_ctx,
					       struct tevent_context *ev,
					       struct smbd_smb2_request *smb2req,
					       uint32_t in_smbpid,
					       uint64_t in_file_id_volatile,
					       DATA_BLOB in_data,
					       uint64_t in_offset,
					       uint32_t in_flags);
static NTSTATUS smbd_smb2_write_recv(struct tevent_req *req,
				     uint32_t *out_count);

static void smbd_smb2_request_write_done(struct tevent_req *subreq);
NTSTATUS smbd_smb2_request_process_write(struct smbd_smb2_request *req)
{
	const uint8_t *inhdr;
	const uint8_t *inbody;
	int i = req->current_idx;
	size_t expected_body_size = 0x31;
	size_t body_size;
	uint32_t in_smbpid;
	uint16_t in_data_offset;
	uint32_t in_data_length;
	DATA_BLOB in_data_buffer;
	uint64_t in_offset;
	uint64_t in_file_id_persistent;
	uint64_t in_file_id_volatile;
	uint32_t in_flags;
	struct tevent_req *subreq;

	inhdr = (const uint8_t *)req->in.vector[i+0].iov_base;
	if (req->in.vector[i+1].iov_len != (expected_body_size & 0xFFFFFFFE)) {
		return smbd_smb2_request_error(req, NT_STATUS_INVALID_PARAMETER);
	}

	inbody = (const uint8_t *)req->in.vector[i+1].iov_base;

	body_size = SVAL(inbody, 0x00);
	if (body_size != expected_body_size) {
		return smbd_smb2_request_error(req, NT_STATUS_INVALID_PARAMETER);
	}

	in_smbpid = IVAL(inhdr, SMB2_HDR_PID);

	in_data_offset		= SVAL(inbody, 0x02);
	in_data_length		= IVAL(inbody, 0x04);
	in_offset		= BVAL(inbody, 0x08);
	in_file_id_persistent	= BVAL(inbody, 0x10);
	in_file_id_volatile	= BVAL(inbody, 0x18);
	in_flags		= IVAL(inbody, 0x2C);

	if (in_data_offset != (SMB2_HDR_BODY + (body_size & 0xFFFFFFFE))) {
		return smbd_smb2_request_error(req, NT_STATUS_INVALID_PARAMETER);
	}

	if (in_data_length > req->in.vector[i+2].iov_len) {
		return smbd_smb2_request_error(req, NT_STATUS_INVALID_PARAMETER);
	}

	/* check the max write size */
	if (in_data_length > 0x00010000) {
		DEBUG(0,("here:%s: 0x%08X: 0x%08X\n",
			__location__, in_data_length, 0x00010000));
		return smbd_smb2_request_error(req, NT_STATUS_INVALID_PARAMETER);
	}

	in_data_buffer.data = (uint8_t *)req->in.vector[i+2].iov_base;
	in_data_buffer.length = in_data_length;

	if (req->compat_chain_fsp) {
		/* skip check */
	} else if (in_file_id_persistent != 0) {
		return smbd_smb2_request_error(req, NT_STATUS_FILE_CLOSED);
	}

	subreq = smbd_smb2_write_send(req,
				      req->sconn->smb2.event_ctx,
				      req,
				      in_smbpid,
				      in_file_id_volatile,
				      in_data_buffer,
				      in_offset,
				      in_flags);
	if (subreq == NULL) {
		return smbd_smb2_request_error(req, NT_STATUS_NO_MEMORY);
	}
	tevent_req_set_callback(subreq, smbd_smb2_request_write_done, req);

	return smbd_smb2_request_pending_queue(req, subreq);
}

static void smbd_smb2_request_write_done(struct tevent_req *subreq)
{
	struct smbd_smb2_request *req = tevent_req_callback_data(subreq,
					struct smbd_smb2_request);
	int i = req->current_idx;
	uint8_t *outhdr;
	DATA_BLOB outbody;
	DATA_BLOB outdyn;
	uint32_t out_count = 0;
	NTSTATUS status;
	NTSTATUS error; /* transport error */

	status = smbd_smb2_write_recv(subreq, &out_count);
	TALLOC_FREE(subreq);
	if (!NT_STATUS_IS_OK(status)) {
		error = smbd_smb2_request_error(req, status);
		if (!NT_STATUS_IS_OK(error)) {
			smbd_server_connection_terminate(req->sconn,
							 nt_errstr(error));
			return;
		}
		return;
	}

	outhdr = (uint8_t *)req->out.vector[i].iov_base;

	outbody = data_blob_talloc(req->out.vector, NULL, 0x10);
	if (outbody.data == NULL) {
		error = smbd_smb2_request_error(req, NT_STATUS_NO_MEMORY);
		if (!NT_STATUS_IS_OK(error)) {
			smbd_server_connection_terminate(req->sconn,
							 nt_errstr(error));
			return;
		}
		return;
	}

	SSVAL(outbody.data, 0x00, 0x10 + 1);	/* struct size */
	SSVAL(outbody.data, 0x02, 0);		/* reserved */
	SIVAL(outbody.data, 0x04, out_count);	/* count */
	SIVAL(outbody.data, 0x08, 0);		/* remaining */
	SSVAL(outbody.data, 0x0C, 0);		/* write channel info offset */
	SSVAL(outbody.data, 0x0E, 0);		/* write channel info length */

	outdyn = data_blob_const(NULL, 0);

	error = smbd_smb2_request_done(req, outbody, &outdyn);
	if (!NT_STATUS_IS_OK(error)) {
		smbd_server_connection_terminate(req->sconn, nt_errstr(error));
		return;
	}
}

struct smbd_smb2_write_state {
	struct smbd_smb2_request *smb2req;
	uint32_t in_length;
	uint32_t out_count;
};

static void smbd_smb2_write_pipe_done(struct tevent_req *subreq);

static struct tevent_req *smbd_smb2_write_send(TALLOC_CTX *mem_ctx,
					       struct tevent_context *ev,
					       struct smbd_smb2_request *smb2req,
					       uint32_t in_smbpid,
					       uint64_t in_file_id_volatile,
					       DATA_BLOB in_data,
					       uint64_t in_offset,
					       uint32_t in_flags)
{
	NTSTATUS status;
	struct tevent_req *req;
	struct smbd_smb2_write_state *state;
	struct smb_request *smbreq;
	connection_struct *conn = smb2req->tcon->compat_conn;
	files_struct *fsp;
	ssize_t nwritten;
	bool write_through = false;
	struct lock_struct lock;

	req = tevent_req_create(mem_ctx, &state,
				struct smbd_smb2_write_state);
	if (req == NULL) {
		return NULL;
	}
	state->smb2req = smb2req;
	state->in_length = in_data.length;
	state->out_count = 0;

	DEBUG(10,("smbd_smb2_write: file_id[0x%016llX]\n",
		  (unsigned long long)in_file_id_volatile));

	smbreq = smbd_smb2_fake_smb_request(smb2req);
	if (tevent_req_nomem(smbreq, req)) {
		return tevent_req_post(req, ev);
	}

	fsp = file_fsp(smbreq, (uint16_t)in_file_id_volatile);
	if (fsp == NULL) {
		tevent_req_nterror(req, NT_STATUS_FILE_CLOSED);
		return tevent_req_post(req, ev);
	}
	if (conn != fsp->conn) {
		tevent_req_nterror(req, NT_STATUS_FILE_CLOSED);
		return tevent_req_post(req, ev);
	}
	if (smb2req->session->vuid != fsp->vuid) {
		tevent_req_nterror(req, NT_STATUS_FILE_CLOSED);
		return tevent_req_post(req, ev);
	}

	if (IS_IPC(smbreq->conn)) {
		struct tevent_req *subreq;

		if (!fsp_is_np(fsp)) {
			tevent_req_nterror(req, NT_STATUS_FILE_CLOSED);
			return tevent_req_post(req, ev);
		}

		subreq = np_write_send(state, smbd_event_context(),
				       fsp->fake_file_handle,
				       in_data.data,
				       in_data.length);
		if (tevent_req_nomem(subreq, req)) {
			return tevent_req_post(req, ev);
		}
		tevent_req_set_callback(subreq,
					smbd_smb2_write_pipe_done,
					req);
		return req;
	}

	if (!CHECK_WRITE(fsp)) {
		tevent_req_nterror(req, NT_STATUS_ACCESS_DENIED);
		return tevent_req_post(req, ev);
	}

	init_strict_lock_struct(fsp,
				in_smbpid,
				in_offset,
				in_data.length,
				WRITE_LOCK,
				&lock);

	if (!SMB_VFS_STRICT_LOCK(conn, fsp, &lock)) {
		tevent_req_nterror(req, NT_STATUS_FILE_LOCK_CONFLICT);
		return tevent_req_post(req, ev);
	}

	nwritten = write_file(smbreq, fsp,
			      (const char *)in_data.data,
			      in_offset,
			      in_data.length);

	if (((nwritten == 0) && (in_data.length != 0)) || (nwritten < 0)) {
		DEBUG(5,("smbd_smb2_write: write_file[%s] disk full\n",
			 fsp_str_dbg(fsp)));
		SMB_VFS_STRICT_UNLOCK(conn, fsp, &lock);
		tevent_req_nterror(req, NT_STATUS_DISK_FULL);
		return tevent_req_post(req, ev);
	}

	DEBUG(3,("smbd_smb2_write: fnum=[%d/%s] length=%d offset=%d wrote=%d\n",
		fsp->fnum, fsp_str_dbg(fsp), (int)in_data.length,
		(int)in_offset, (int)nwritten));

	if (in_flags & 0x00000001) {
		write_through = true;
	}

	status = sync_file(conn, fsp, write_through);
	if (!NT_STATUS_IS_OK(status)) {
		DEBUG(5,("smbd_smb2_write: sync_file for %s returned %s\n",
			fsp_str_dbg(fsp), nt_errstr(status)));
		SMB_VFS_STRICT_UNLOCK(conn, fsp, &lock);
		tevent_req_nterror(req, status);
		return tevent_req_post(req, ev);
	}

	SMB_VFS_STRICT_UNLOCK(conn, fsp, &lock);

	state->out_count = nwritten;

	tevent_req_done(req);
	return tevent_req_post(req, ev);
}

static void smbd_smb2_write_pipe_done(struct tevent_req *subreq)
{
	struct tevent_req *req = tevent_req_callback_data(subreq,
				 struct tevent_req);
	struct smbd_smb2_write_state *state = tevent_req_data(req,
					      struct smbd_smb2_write_state);
	NTSTATUS status;
	ssize_t nwritten = -1;

	status = np_write_recv(subreq, &nwritten);
	TALLOC_FREE(subreq);
	if (!NT_STATUS_IS_OK(status)) {
		tevent_req_nterror(req, status);
		return;
	}

	if ((nwritten == 0 && state->in_length != 0) || (nwritten < 0)) {
		tevent_req_nterror(req, NT_STATUS_ACCESS_DENIED);
		return;
	}

	state->out_count = nwritten;

	tevent_req_done(req);
}

static NTSTATUS smbd_smb2_write_recv(struct tevent_req *req,
				     uint32_t *out_count)
{
	NTSTATUS status;
	struct smbd_smb2_write_state *state = tevent_req_data(req,
					      struct smbd_smb2_write_state);

	if (tevent_req_is_nterror(req, &status)) {
		tevent_req_received(req);
		return status;
	}

	*out_count = state->out_count;

	tevent_req_received(req);
	return NT_STATUS_OK;
}
