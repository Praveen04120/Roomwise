/**
 * Roomwise — Empty Classroom Finder
 * Admin Portal JavaScript Client
 */

(function () {
  'use strict';

  const API_BASE = (window.ROOMWISE_API_URL || '').replace(/\/+$/, '');
  function apiUrl(path) {
    return `${API_BASE}${path}`;
  }

  const RoomwiseAdmin = {
    token: null,
    currentTab: 'dashboard',
    floors: [],
    rooms: [],
    validatedCSV: '',
    parsedValidation: null,

    init: async function () {
      const csvDash = document.getElementById('sampleCsvLinkDash');
      const csvUpload = document.getElementById('sampleCsvLinkUpload');
      if (csvDash) csvDash.href = apiUrl('/sample_timetable.csv');
      if (csvUpload) csvUpload.href = apiUrl('/sample_timetable.csv');

      document.addEventListener('keydown', (e) => {
        if (e.key === 'Escape') this.closeModals();
      });

      this.token = sessionStorage.getItem('roomwise_admin_token');
      if (this.token) {
        const ok = await this.verifySession();
        if (ok) {
          this.showAdminApp();
          return;
        }
      }
      this.showLoginScreen();
    },

    showLoginScreen: function () {
      document.getElementById('adminLoginScreen').style.display = 'flex';
      document.getElementById('adminAppWrap').style.display = 'none';
      const pwdInput = document.getElementById('adminPasswordInput');
      if (pwdInput) {
        pwdInput.value = '';
        pwdInput.focus();
      }
    },

    showAdminApp: function () {
      document.getElementById('adminLoginScreen').style.display = 'none';
      document.getElementById('adminAppWrap').style.display = 'block';
      this.switchTab('dashboard');
    },

    authFetch: async function (path, options = {}) {
      if (!options.headers) options.headers = {};
      if (this.token) {
        options.headers['Authorization'] = `Bearer ${this.token}`;
        options.headers['X-Admin-Token'] = this.token;
      }
      const res = await fetch(apiUrl(path), options);
      if (res.status === 401) {
        this.showToast('Session expired or unauthorized. Please log in again.', 'error');
        this.token = null;
        sessionStorage.removeItem('roomwise_admin_token');
        this.showLoginScreen();
        throw new Error('Unauthorized');
      }
      return res;
    },

    verifySession: async function () {
      try {
        const res = await fetch(apiUrl('/api/admin/dashboard'), {
          headers: {
            'Authorization': `Bearer ${this.token}`,
            'X-Admin-Token': this.token
          }
        });
        if (!res.ok) {
          this.token = null;
          sessionStorage.removeItem('roomwise_admin_token');
          return false;
        }
        return true;
      } catch {
        return false;
      }
    },

    login: async function () {
      const pwdInput = document.getElementById('adminPasswordInput');
      const password = pwdInput ? pwdInput.value.trim() : '';

      if (!password) {
        this.showToast('Please enter administrator password.', 'error');
        return;
      }

      try {
        const res = await fetch(apiUrl('/api/admin/login'), {
          method: 'POST',
          headers: { 'Content-Type': 'application/json' },
          body: JSON.stringify({ password })
        });

        const data = await res.json();
        if (res.ok && data.success) {
          this.token = data.token;
          sessionStorage.setItem('roomwise_admin_token', this.token);
          this.showToast('Authentication successful!', 'success');
          this.showAdminApp();
        } else {
          this.showToast(data.error || 'Invalid administrator password.', 'error');
        }
      } catch (err) {
        console.error('Login error:', err);
        this.showToast('Network error during login.', 'error');
      }
    },

    logout: async function () {
      if (this.token) {
        try {
          await fetch(apiUrl('/api/admin/logout'), {
            method: 'POST',
            headers: {
              'Authorization': `Bearer ${this.token}`,
              'X-Admin-Token': this.token
            }
          });
        } catch {}
      }
      this.token = null;
      sessionStorage.removeItem('roomwise_admin_token');
      this.showToast('Logged out successfully.', 'info');
      this.showLoginScreen();
    },

    switchTab: async function (tabName) {
      this.currentTab = tabName;

      document.querySelectorAll('.admin-tab-btn').forEach(btn => {
        btn.classList.toggle('active', btn.dataset.tab === tabName);
      });

      document.querySelectorAll('.admin-panel').forEach(p => p.classList.remove('active'));
      const panel = document.getElementById(`tab-${tabName}`);
      if (panel) panel.classList.add('active');

      if (tabName === 'dashboard') await this.loadDashboard();
      else if (tabName === 'floors') await this.loadFloorsTable();
      else if (tabName === 'rooms') await this.loadRoomsTable();
      else if (tabName === 'timetable') await this.loadTimetableTable();
      else if (tabName === 'settings') await this.loadSettingsForm();
    },

    // -------------------------------------------------------------
    // Dashboard Tab
    // -------------------------------------------------------------
    loadDashboard: async function () {
      try {
        const res = await this.authFetch('/api/admin/dashboard');
        const data = await res.json();

        document.getElementById('dashTotalFloors').textContent = data.total_floors;
        document.getElementById('dashTotalRooms').textContent = data.total_rooms;
        document.getElementById('dashTotalEntries').textContent = data.total_timetable_entries;
        document.getElementById('dashUpcomingCount').textContent = data.upcoming_classes_today ? data.upcoming_classes_today.length : 0;
        document.getElementById('dashTodayDay').textContent = data.today_day || 'Today';

        if (data.settings) {
          const headerCampus = document.getElementById('adminCampusHeader');
          if (headerCampus) headerCampus.textContent = `${data.settings.college_name} · ${data.settings.building_name}`;
        }

        const tbody = document.getElementById('dashUpcomingTableBody');
        tbody.innerHTML = '';

        if (!data.upcoming_classes_today || data.upcoming_classes_today.length === 0) {
          tbody.innerHTML = '<tr><td colspan="4" style="text-align: center; color: var(--text-muted); padding: 20px;">No remaining classes scheduled for today.</td></tr>';
        } else {
          data.upcoming_classes_today.forEach(c => {
            const tr = document.createElement('tr');
            tr.innerHTML = `
              <td><strong style="color: var(--primary-900);">Room ${this.escapeHtml(c.room_number)}</strong></td>
              <td style="white-space: nowrap;">${this.escapeHtml(c.start_time)} – ${this.escapeHtml(c.end_time)}</td>
              <td><span class="logo-badge">${this.escapeHtml(c.section)}</span></td>
              <td>${this.escapeHtml(c.subject)}</td>
            `;
            tbody.appendChild(tr);
          });
        }
      } catch (err) {
        console.error('Error loading dashboard:', err);
      }
    },

    // -------------------------------------------------------------
    // Manage Floors Tab
    // -------------------------------------------------------------
    loadFloorsTable: async function () {
      try {
        const res = await this.authFetch('/api/floors');
        this.floors = await res.json();

        const tbody = document.getElementById('adminFloorsTableBody');
        tbody.innerHTML = '';

        this.floors.forEach(f => {
          const tr = document.createElement('tr');
          tr.innerHTML = `
            <td><strong style="font-size: 1.05rem; color: var(--primary-900);">Floor ${f.floor_number}</strong></td>
            <td><strong>${this.escapeHtml(f.floor_name)}</strong></td>
            <td style="color: var(--text-muted); font-size: 0.85rem;">${this.escapeHtml(f.description || '—')}</td>
            <td><span class="logo-badge">${f.room_count ?? 0} Rooms</span></td>
            <td>
              <div class="action-buttons">
                <button type="button" class="btn btn-secondary btn-sm btn-edit-floor">Edit</button>
                <button type="button" class="btn btn-secondary btn-sm btn-delete-floor" style="color: var(--status-occ);">Delete</button>
              </div>
            </td>
          `;
          tr.querySelector('.btn-edit-floor')?.addEventListener('click', () => this.openEditFloorModal(f.floor_number));
          tr.querySelector('.btn-delete-floor')?.addEventListener('click', () => this.deleteFloor(f.floor_number));
          tbody.appendChild(tr);
        });
      } catch (err) {
        console.error('Error loading floors table:', err);
      }
    },

    openAddFloorModal: function () {
      document.getElementById('floorModalTitle').textContent = 'Add New Floor';
      const maxFloor = this.floors.reduce((acc, f) => Math.max(acc, f.floor_number), 0);
      document.getElementById('modalFloorNumber').value = maxFloor + 1;
      document.getElementById('modalFloorNumber').readOnly = false;
      document.getElementById('modalFloorName').value = '';
      document.getElementById('modalFloorDesc').value = '';
      document.getElementById('floorModalOverlay').classList.add('active');
    },

    openEditFloorModal: function (floorNumber) {
      const f = this.floors.find(x => x.floor_number === floorNumber);
      if (!f) return;
      document.getElementById('floorModalTitle').textContent = `Edit Floor ${floorNumber}`;
      document.getElementById('modalFloorNumber').value = f.floor_number;
      document.getElementById('modalFloorNumber').readOnly = true;
      document.getElementById('modalFloorName').value = f.floor_name;
      document.getElementById('modalFloorDesc').value = f.description || '';
      document.getElementById('floorModalOverlay').classList.add('active');
    },

    saveFloor: async function () {
      const num = parseInt(document.getElementById('modalFloorNumber').value, 10);
      const name = document.getElementById('modalFloorName').value.trim();
      const desc = document.getElementById('modalFloorDesc').value.trim();
      const isEdit = document.getElementById('modalFloorNumber').readOnly;

      if (!name) {
        this.showToast('Please provide a floor name.', 'error');
        return;
      }

      try {
        const url = isEdit ? `/api/admin/floors/${num}` : '/api/admin/floors';
        const method = isEdit ? 'PUT' : 'POST';

        const res = await this.authFetch(url, {
          method,
          headers: { 'Content-Type': 'application/json' },
          body: JSON.stringify({ floor_number: num, floor_name: name, description: desc })
        });

        const data = await res.json();
        if (res.ok) {
          this.showToast(`Floor ${num} saved successfully!`, 'success');
          this.closeModals();
          await this.loadFloorsTable();
        } else {
          this.showToast(data.error || 'Failed to save floor.', 'error');
        }
      } catch (err) {
        console.error('Error saving floor:', err);
      }
    },

    deleteFloor: async function (floorNumber) {
      if (!confirm(`Are you sure you want to delete Floor ${floorNumber}? This action cannot be undone.`)) {
        return;
      }

      try {
        const res = await this.authFetch(`/api/admin/floors/${floorNumber}`, { method: 'DELETE' });
        const data = await res.json();
        if (res.ok) {
          this.showToast(`Floor ${floorNumber} deleted.`, 'success');
          await this.loadFloorsTable();
        } else {
          this.showToast(data.error || 'Cannot delete floor.', 'error');
        }
      } catch (err) {
        console.error('Error deleting floor:', err);
      }
    },

    // -------------------------------------------------------------
    // Manage Classrooms Tab
    // -------------------------------------------------------------
    loadRoomsTable: async function () {
      try {
        const fRes = await this.authFetch('/api/floors');
        this.floors = await fRes.json();

        const filterSelect = document.getElementById('adminRoomFloorFilter');
        if (filterSelect) {
          const currentVal = filterSelect.value || '0';
          filterSelect.innerHTML = '<option value="0">All Floors</option>';
          this.floors.forEach(f => {
            const opt = document.createElement('option');
            opt.value = f.floor_number;
            opt.textContent = `Floor ${f.floor_number} (${f.floor_name})`;
            filterSelect.appendChild(opt);
          });
          filterSelect.value = currentVal;
        }

        const floorVal = filterSelect ? filterSelect.value : '0';
        const res = await this.authFetch(`/api/rooms?floor=${encodeURIComponent(floorVal)}`);
        this.rooms = await res.json();

        const tbody = document.getElementById('adminRoomsTableBody');
        tbody.innerHTML = '';

        if (this.rooms.length === 0) {
          tbody.innerHTML = '<tr><td colspan="6" style="text-align: center; color: var(--text-muted); padding: 20px;">No classrooms found.</td></tr>';
          return;
        }

        this.rooms.forEach(r => {
          const tr = document.createElement('tr');
          tr.innerHTML = `
            <td><strong style="color: var(--primary-900); font-size: 1.05rem;">Room ${this.escapeHtml(r.room_number)}</strong></td>
            <td>Floor ${r.floor_number}</td>
            <td>${this.escapeHtml(r.room_type || 'Classroom')}</td>
            <td>${r.capacity} Seats</td>
            <td>
              <span class="status-badge ${r.has_schedule ? 'status-available' : 'status-unavailable'}">
                ${r.has_schedule ? 'Configured' : 'No Schedule'}
              </span>
            </td>
            <td>
              <div class="action-buttons">
                <button type="button" class="btn btn-secondary btn-sm btn-edit-room">Edit</button>
                <button type="button" class="btn btn-secondary btn-sm btn-delete-room" style="color: var(--status-occ);">Delete</button>
              </div>
            </td>
          `;
          tr.querySelector('.btn-edit-room')?.addEventListener('click', () => this.openEditRoomModal(r.room_number));
          tr.querySelector('.btn-delete-room')?.addEventListener('click', () => this.deleteRoom(r.room_number));
          tbody.appendChild(tr);
        });
      } catch (err) {
        console.error('Error loading rooms table:', err);
      }
    },

    openAddRoomModal: async function () {
      const res = await this.authFetch('/api/floors');
      this.floors = await res.json();

      document.getElementById('roomModalTitle').textContent = 'Add Classroom';
      document.getElementById('modalRoomOriginalNumber').value = '';
      document.getElementById('modalRoomNumber').value = '';
      document.getElementById('modalRoomCapacity').value = '60';

      const floorSelect = document.getElementById('modalRoomFloor');
      floorSelect.innerHTML = '';
      this.floors.forEach(f => {
        const opt = document.createElement('option');
        opt.value = f.floor_number;
        opt.textContent = `Floor ${f.floor_number} — ${f.floor_name}`;
        floorSelect.appendChild(opt);
      });

      document.getElementById('roomModalOverlay').classList.add('active');
    },

    openEditRoomModal: async function (roomNumber) {
      const room = this.rooms.find(r => r.room_number === roomNumber);
      if (!room) return;

      document.getElementById('roomModalTitle').textContent = `Edit Classroom ${roomNumber}`;
      document.getElementById('modalRoomOriginalNumber').value = room.room_number;
      document.getElementById('modalRoomNumber').value = room.room_number;
      document.getElementById('modalRoomCapacity').value = room.capacity || 60;
      document.getElementById('modalRoomType').value = room.room_type || 'Standard Classroom';

      const floorSelect = document.getElementById('modalRoomFloor');
      floorSelect.innerHTML = '';
      this.floors.forEach(f => {
        const opt = document.createElement('option');
        opt.value = f.floor_number;
        opt.textContent = `Floor ${f.floor_number} — ${f.floor_name}`;
        if (f.floor_number === room.floor_number) opt.selected = true;
        floorSelect.appendChild(opt);
      });

      document.getElementById('roomModalOverlay').classList.add('active');
    },

    saveRoom: async function () {
      const origNum = document.getElementById('modalRoomOriginalNumber').value.trim();
      const roomNum = document.getElementById('modalRoomNumber').value.trim();
      const floorNum = parseInt(document.getElementById('modalRoomFloor').value, 10);
      const capacity = parseInt(document.getElementById('modalRoomCapacity').value, 10);
      const roomType = document.getElementById('modalRoomType').value;

      if (!roomNum) {
        this.showToast('Please enter room number.', 'error');
        return;
      }

      const isEdit = !!origNum;
      const url = isEdit ? `/api/admin/rooms/${encodeURIComponent(origNum)}` : '/api/admin/rooms';
      const method = isEdit ? 'PUT' : 'POST';

      try {
        const res = await this.authFetch(url, {
          method,
          headers: { 'Content-Type': 'application/json' },
          body: JSON.stringify({
            room_number: roomNum,
            floor_number: floorNum,
            capacity: capacity,
            room_type: roomType,
            is_active: true
          })
        });

        const data = await res.json();
        if (res.ok) {
          this.showToast(`Classroom ${roomNum} saved!`, 'success');
          this.closeModals();
          if (this.currentTab === 'rooms') await this.loadRoomsTable();
          else await this.loadDashboard();
        } else {
          this.showToast(data.error || 'Failed to save classroom.', 'error');
        }
      } catch (err) {
        console.error('Error saving room:', err);
      }
    },

    deleteRoom: async function (roomNumber) {
      if (!confirm(`Are you sure you want to delete Classroom ${roomNumber}? Its scheduled classes will also be deleted.`)) {
        return;
      }

      try {
        const res = await this.authFetch(`/api/admin/rooms/${encodeURIComponent(roomNumber)}`, { method: 'DELETE' });
        const data = await res.json();
        if (res.ok) {
          this.showToast(`Classroom ${roomNumber} deleted.`, 'success');
          await this.loadRoomsTable();
        } else {
          this.showToast(data.error || 'Cannot delete room.', 'error');
        }
      } catch (err) {
        console.error('Error deleting room:', err);
      }
    },

    // -------------------------------------------------------------
    // Upload Timetable Tab (CSV Validation & Import)
    // -------------------------------------------------------------
    handleFileSelected: function (files) {
      if (!files || files.length === 0) return;
      const file = files[0];
      document.getElementById('selectedFileName').textContent = `${file.name} (${Math.max(1, Math.round(file.size / 1024))} KB)`;

      const reader = new FileReader();
      reader.onload = (e) => {
        document.getElementById('csvTextInput').value = e.target.result;
      };
      reader.readAsText(file);
    },

    clearCSV: function () {
      document.getElementById('csvTextInput').value = '';
      document.getElementById('selectedFileName').textContent = 'No file chosen';
      const fileInput = document.getElementById('csvFileInput');
      if (fileInput) fileInput.value = '';
      document.getElementById('csvValidationBox').style.display = 'none';
      this.validatedCSV = '';
      this.parsedValidation = null;
    },

    validateCSV: async function () {
      const content = document.getElementById('csvTextInput').value.trim();
      if (!content) {
        this.showToast('Please select a CSV file or paste CSV content first.', 'error');
        return;
      }

      try {
        const res = await this.authFetch('/api/admin/timetable/validate', {
          method: 'POST',
          headers: { 'Content-Type': 'application/json' },
          body: JSON.stringify({ csv_content: content })
        });

        const data = await res.json();
        if (!res.ok) {
          this.showToast(data.error || 'CSV validation failed.', 'error');
          return;
        }

        this.validatedCSV = content;
        this.parsedValidation = data;

        document.getElementById('csvValidBadge').textContent = `${data.valid_rows} Valid Rows`;
        document.getElementById('csvInvalidBadge').textContent = `${data.invalid_rows} Invalid Rows`;

        const errBox = document.getElementById('csvSummaryErrors');
        if (errBox) {
          if (data.summary_errors && data.summary_errors.length > 0) {
            errBox.textContent = data.summary_errors.join(' ');
            errBox.style.display = 'block';
          } else {
            errBox.style.display = 'none';
          }
        }

        const tbody = document.getElementById('csvPreviewTableBody');
        tbody.innerHTML = '';

        data.rows.forEach(r => {
          const tr = document.createElement('tr');
          if (!r.is_valid) {
            tr.style.backgroundColor = 'var(--status-occ-bg)';
          } else if (r.conflicts_existing) {
            tr.style.backgroundColor = '#fffbeb';
          } else {
            tr.style.backgroundColor = 'var(--status-avail-bg)';
          }

          const msgHtml = !r.is_valid
            ? this.escapeHtml(r.error_message)
            : (r.conflicts_existing ? this.escapeHtml(r.warning_message) : 'Ready to import');

          const badgeClass = !r.is_valid ? 'status-occupied' : (r.conflicts_existing ? 'status-unavailable' : 'status-available');
          const badgeText = !r.is_valid ? 'INVALID' : (r.conflicts_existing ? 'OVERLAP' : 'VALID');

          tr.innerHTML = `
            <td>#${r.row_number}</td>
            <td><span class="status-badge ${badgeClass}">${badgeText}</span></td>
            <td><strong>Room ${this.escapeHtml(r.room_number)}</strong></td>
            <td>${this.escapeHtml(r.day)}</td>
            <td style="white-space: nowrap;">${this.escapeHtml(r.start_time)} – ${this.escapeHtml(r.end_time)}</td>
            <td>${this.escapeHtml(r.section)}</td>
            <td>${this.escapeHtml(r.subject)}</td>
            <td style="font-size: 0.8rem; color: ${r.is_valid ? 'var(--text-main)' : 'var(--status-occ-text)'};">${msgHtml}</td>
          `;
          tbody.appendChild(tr);
        });

        document.getElementById('csvValidationBox').style.display = 'block';
        document.getElementById('csvValidationBox').scrollIntoView({ behavior: 'smooth' });

        if (data.can_import) {
          this.showToast(`CSV analyzed: ${data.valid_rows} valid, ${data.invalid_rows} invalid.`, 'info');
        } else {
          this.showToast('No valid rows detected in CSV file.', 'error');
        }
      } catch (err) {
        console.error('Validation error:', err);
      }
    },

    commitImport: async function () {
      if (!this.validatedCSV || !this.parsedValidation || !this.parsedValidation.can_import) {
        this.showToast('Please validate a CSV with valid rows before importing.', 'error');
        return;
      }

      const replaceMode = document.querySelector('input[name="importModeRadio"]:checked')?.value === 'replace';

      if (replaceMode) {
        if (!confirm('WARNING: You chose to REPLACE the entire timetable. All existing schedule entries will be deleted and replaced with valid CSV rows. Proceed?')) {
          return;
        }
      }

      try {
        const res = await this.authFetch('/api/admin/timetable/import', {
          method: 'POST',
          headers: { 'Content-Type': 'application/json' },
          body: JSON.stringify({
            csv_content: this.validatedCSV,
            replace: replaceMode
          })
        });

        const data = await res.json();
        if (res.ok && data.success) {
          const warnNote = data.warnings && data.warnings.length > 0
            ? ` (${data.warnings.length} skipped due to conflicts/errors)`
            : '';
          this.showToast(`Imported ${data.imported_count} timetable entries (${data.mode})${warnNote}.`, 'success');
          this.clearCSV();
          await this.switchTab('timetable');
        } else {
          this.showToast(data.error || 'Import failed.', 'error');
        }
      } catch (err) {
        console.error('Commit import error:', err);
      }
    },

    // -------------------------------------------------------------
    // Manage Timetable Tab (CRUD)
    // -------------------------------------------------------------
    loadTimetableTable: async function () {
      const roomFilter = document.getElementById('adminFilterRoom')?.value.trim() || '';
      const dayFilter = document.getElementById('adminFilterDay')?.value || '';

      try {
        let url = '/api/admin/timetable?';
        if (roomFilter) url += `room=${encodeURIComponent(roomFilter)}&`;
        if (dayFilter) url += `day=${encodeURIComponent(dayFilter)}&`;

        const res = await this.authFetch(url);
        const entries = await res.json();

        const tbody = document.getElementById('adminTimetableTableBody');
        tbody.innerHTML = '';

        if (entries.length === 0) {
          tbody.innerHTML = '<tr><td colspan="6" style="text-align: center; color: var(--text-muted); padding: 20px;">No timetable entries match filter.</td></tr>';
          return;
        }

        entries.forEach(e => {
          const tr = document.createElement('tr');
          tr.innerHTML = `
            <td><strong style="color: var(--primary-900);">Room ${this.escapeHtml(e.room_number)}</strong></td>
            <td><span class="logo-badge">${this.escapeHtml(e.day)}</span></td>
            <td style="white-space: nowrap;"><strong>${this.escapeHtml(e.start_time)} – ${this.escapeHtml(e.end_time)}</strong></td>
            <td>${this.escapeHtml(e.section)}</td>
            <td>${this.escapeHtml(e.subject)}</td>
            <td>
              <div class="action-buttons">
                <button type="button" class="btn btn-secondary btn-sm btn-edit-entry">Edit</button>
                <button type="button" class="btn btn-secondary btn-sm btn-delete-entry" style="color: var(--status-occ);">Delete</button>
              </div>
            </td>
          `;
          tr.querySelector('.btn-edit-entry')?.addEventListener('click', () => this.openEditScheduleModal(e.id));
          tr.querySelector('.btn-delete-entry')?.addEventListener('click', () => this.deleteScheduleEntry(e.id));
          tbody.appendChild(tr);
        });
      } catch (err) {
        console.error('Error loading timetable table:', err);
      }
    },

    openAddScheduleModal: function () {
      document.getElementById('scheduleModalTitle').textContent = 'Schedule Lecture';
      document.getElementById('modalScheduleId').value = '';
      document.getElementById('modalScheduleRoom').value = '';
      document.getElementById('modalScheduleDay').value = 'Monday';
      document.getElementById('modalScheduleSection').value = '';
      document.getElementById('modalScheduleSubject').value = '';
      document.getElementById('modalScheduleStartTime').value = '09:00';
      document.getElementById('modalScheduleEndTime').value = '10:00';
      document.getElementById('scheduleModalOverlay').classList.add('active');
    },

    openEditScheduleModal: async function (entryId) {
      try {
        const res = await this.authFetch('/api/admin/timetable');
        const entries = await res.json();
        const entry = entries.find(x => x.id === entryId);
        if (!entry) return;

        document.getElementById('scheduleModalTitle').textContent = 'Edit Schedule Entry';
        document.getElementById('modalScheduleId').value = entry.id;
        document.getElementById('modalScheduleRoom').value = entry.room_number;
        document.getElementById('modalScheduleDay').value = entry.day;
        document.getElementById('modalScheduleSection').value = entry.section;
        document.getElementById('modalScheduleSubject').value = entry.subject;
        document.getElementById('modalScheduleStartTime').value = entry.start_time;
        document.getElementById('modalScheduleEndTime').value = entry.end_time;
        document.getElementById('scheduleModalOverlay').classList.add('active');
      } catch (err) {
        console.error('Error opening edit schedule modal:', err);
      }
    },

    saveScheduleEntry: async function () {
      const id = document.getElementById('modalScheduleId').value;
      const room = document.getElementById('modalScheduleRoom').value.trim();
      const day = document.getElementById('modalScheduleDay').value;
      const section = document.getElementById('modalScheduleSection').value.trim();
      const subject = document.getElementById('modalScheduleSubject').value.trim();
      const startTime = document.getElementById('modalScheduleStartTime').value;
      const endTime = document.getElementById('modalScheduleEndTime').value;

      if (!room || !section || !subject || !startTime || !endTime) {
        this.showToast('Please fill out all schedule fields.', 'error');
        return;
      }

      const isEdit = !!id;
      const url = isEdit ? `/api/admin/timetable/${encodeURIComponent(id)}` : '/api/admin/timetable';
      const method = isEdit ? 'PUT' : 'POST';

      try {
        const res = await this.authFetch(url, {
          method,
          headers: { 'Content-Type': 'application/json' },
          body: JSON.stringify({
            room_number: room,
            day: day,
            section: section,
            subject: subject,
            start_time: startTime,
            end_time: endTime
          })
        });

        const data = await res.json();
        if (res.ok) {
          this.showToast('Schedule saved successfully!', 'success');
          this.closeModals();
          if (this.currentTab === 'timetable') await this.loadTimetableTable();
          else await this.loadDashboard();
        } else {
          this.showToast(data.error || 'Failed to save schedule.', 'error');
        }
      } catch (err) {
        console.error('Error saving schedule entry:', err);
      }
    },

    deleteScheduleEntry: async function (entryId) {
      if (!confirm('Are you sure you want to delete this schedule entry?')) return;

      try {
        const res = await this.authFetch(`/api/admin/timetable/${encodeURIComponent(entryId)}`, { method: 'DELETE' });
        const data = await res.json();
        if (res.ok) {
          this.showToast('Schedule entry deleted.', 'success');
          await this.loadTimetableTable();
        } else {
          this.showToast(data.error || 'Failed to delete schedule entry.', 'error');
        }
      } catch (err) {
        console.error('Error deleting schedule entry:', err);
      }
    },

    // -------------------------------------------------------------
    // Settings Tab
    // -------------------------------------------------------------
    loadSettingsForm: async function () {
      try {
        const res = await this.authFetch('/api/admin/settings');
        const s = await res.json();

        document.getElementById('setCollegeName').value = s.college_name || '';
        document.getElementById('setBuildingName').value = s.building_name || '';
        document.getElementById('setOpStart').value = s.operating_start || '08:00';
        document.getElementById('setOpEnd').value = s.operating_end || '18:00';
        document.getElementById('setAdminPassword').value = '';
      } catch (err) {
        console.error('Error loading settings:', err);
      }
    },

    saveSettings: async function () {
      const cName = document.getElementById('setCollegeName').value.trim();
      const bName = document.getElementById('setBuildingName').value.trim();
      const opStart = document.getElementById('setOpStart').value;
      const opEnd = document.getElementById('setOpEnd').value;
      const pwd = document.getElementById('setAdminPassword').value.trim();

      const payload = {
        college_name: cName,
        building_name: bName,
        operating_start: opStart,
        operating_end: opEnd
      };
      if (pwd) {
        payload.admin_password = pwd;
      }

      try {
        const res = await this.authFetch('/api/admin/settings', {
          method: 'PUT',
          headers: { 'Content-Type': 'application/json' },
          body: JSON.stringify(payload)
        });

        const data = await res.json();
        if (res.ok) {
          this.showToast('Configuration settings updated successfully!', 'success');
          document.getElementById('setAdminPassword').value = '';
        } else {
          this.showToast(data.error || 'Failed to save settings.', 'error');
        }
      } catch (err) {
        console.error('Error saving settings:', err);
      }
    },

    closeModals: function () {
      document.querySelectorAll('.modal-overlay').forEach(m => m.classList.remove('active'));
    },

    showToast: function (msg, type = 'info') {
      const container = document.getElementById('adminToastContainer');
      if (!container) return;

      const toast = document.createElement('div');
      toast.className = `toast toast-${type}`;
      toast.textContent = msg;

      container.appendChild(toast);
      setTimeout(() => {
        toast.style.opacity = '0';
        setTimeout(() => toast.remove(), 300);
      }, 4500);
    },

    escapeHtml: function (str) {
      if (str === null || str === undefined) return '';
      return String(str)
        .replace(/&/g, '&amp;')
        .replace(/</g, '&lt;')
        .replace(/>/g, '&gt;')
        .replace(/"/g, '&quot;')
        .replace(/'/g, '&#39;');
    }
  };

  window.RoomwiseAdmin = RoomwiseAdmin;
  document.addEventListener('DOMContentLoaded', () => RoomwiseAdmin.init());
})();
